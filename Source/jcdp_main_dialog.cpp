/*
This file is part of CDP Front-end.

CDP front-end is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

CDP front-end is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with CDP front-end.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "jcdp_main_dialog.h"
#include "reaper_plugin_functions.h"
#include <set>
#include <future>

#ifndef WIN32
#include "stdlib.h"
#endif

#undef min
#undef max

extern File g_cdp_binaries_dir;
extern File g_stand_alone_render_dir;
extern bool g_is_running_as_plugin;
extern std::unique_ptr<PropertiesFile> g_propsfile;

int g_max_child_process_wait_time=15000;

ValueTree serialize_to_value_tree(const envelope_node& pt, Identifier id)
{
	ValueTree vt(id);
	vt.setProperty("t", pt.Time,nullptr);
	vt.setProperty("v", pt.Value, nullptr);
	vt.setProperty("p0", pt.ShapeParam1, nullptr);
	return vt;
}

ValueTree serialize_to_value_tree(const breakpoint_envelope& env, Identifier id)
{
	ValueTree vt(id);
	for (int i = 0; i < env.GetNumNodes(); ++i)
	{
		ValueTree pt_t = serialize_to_value_tree(env.GetNodeAtIndex(i), "pt");
		vt.addChild(pt_t, -1, nullptr);
	}
	return vt;
}

void deserialize_from_value_tree(breakpoint_envelope& env, ValueTree vt)
{
	if (vt.isValid() == true)
	{
		env.ClearAllNodes();
		for (int i = 0; i < vt.getNumChildren(); ++i)
		{
			ValueTree pt_tree = vt.getChild(i);
			double t = pt_tree.getProperty("t");
			double v = pt_tree.getProperty("v");
			double p0 = pt_tree.getProperty("p0");
			env.AddNode(envelope_node(t, v, p0));
		}
	}
}

ValueTree serialize_to_value_tree(const CDP_processor_info& p, Identifier id)
{
	ValueTree vt(id);
	ValueTree params_vt("parameters");
	vt.addChild(params_vt, -1, nullptr);
	for (auto& par : p.m_parameters)
	{
		ValueTree partree(make_valid_id_string(par.m_name));
		partree.setProperty("value", par.m_current_value, nullptr);
		partree.setProperty("env_enabled", par.m_automation_enabled, nullptr);
		ValueTree envelope_tree = serialize_to_value_tree(par.m_env,"envelope");
		partree.addChild(envelope_tree, -1, nullptr);
		params_vt.addChild(partree, -1, nullptr);
	}
	return vt;
}

bool deserialize_from_value_tree(CDP_processor_info& p, ValueTree vt)
{
	bool did_change = false;
	ValueTree state_vt = vt.getChildWithName("state");
	ValueTree parameters_vt = state_vt.getChildWithName("parameters");
	for (int i = 0; i < p.m_parameters.size(); ++i)
	{
		String vtparname = make_valid_id_string(p.m_parameters[i].m_name);
		ValueTree paramvt = parameters_vt.getChildWithName(vtparname);
		if (paramvt.isValid() == true)
		{
			double old_value = p.m_parameters[i].m_current_value;
			double recalled_value = paramvt.getProperty("value");
			if (recalled_value != old_value)
			{
				p.m_parameters[i].m_current_value = paramvt.getProperty("value");
				did_change = true;
			}
			bool env_enabled = paramvt.getProperty("env_enabled");
			p.m_parameters[i].m_automation_enabled = env_enabled;
			ValueTree env_vt = paramvt.getChildWithName("envelope");
			deserialize_from_value_tree(p.m_parameters[i].m_env, env_vt);
		}
		
	}
	return true;
}

void choose_rendering_location()
{
    FileChooser chooser("Choose rendering location");
    bool result=chooser.browseForDirectory();
    if (result==true)
    {
        g_stand_alone_render_dir=chooser.getResult();
        g_propsfile->setValue("render_dir",g_stand_alone_render_dir.getFullPathName());
    }
}

cdp_main_dialog::cdp_main_dialog(IJCDPreviewPlayback *delegate, 
	std::vector<CDP_processor_info> *proc_infos, KnownPluginList* kplist) :
    ResizableWindow(String("CDP Front-end (pre9)"),Colours::darkgrey,true),
    m_audio_delegate(delegate), m_proc_infos(proc_infos),
	m_presets_value_tree("presets"), m_kplist(kplist)
  //m_commands(this)
{
	m_presets_value_tree.setProperty("version", 0, nullptr);
	m_audio_delegate->set_looped(g_propsfile->getBoolValue("looped_preview", true));
	m_follow_item_selection = false; // g_propsfile->getBoolValue("follow_item_selection", true);
    m_render_timer_enabled=g_propsfile->getBoolValue("autorender",true);
    g_max_child_process_wait_time=1000*g_propsfile->getIntValue("cdp_max_wait",15);
    Logger::writeToLog("max process wait time "+String(g_max_child_process_wait_time));
	m_gui_scale_factor = g_propsfile->getDoubleValue("gui_scale_factor", 1.0);
#ifdef WIN32
    m_env_bsize=g_propsfile->getValue("cdp_buf_size","1024");
    auto winresult=SetEnvironmentVariableA("CDP_MEMORY_BBSIZE",m_env_bsize.toRawUTF8());
    if (winresult==0)
        Logger::writeToLog("could not set cdp buf size envvar");
#else
    m_env_bsize=String("CDP_MEMORY_BBSIZE=")+g_propsfile->getValue("cdp_buf_size","1024");
    if (putenv((char*)m_env_bsize.toRawUTF8())!=0)
        Logger::writeToLog("could not set cdp buf size envvar");
#endif
    if (g_is_running_as_plugin==false)
        setResizable(true,true);
    else
    {
        //#ifdef WIN32
        //setResizable(true,true);
        //#endif
    }
#ifndef NDEBUG
    setName("This is a DEBUG build!!!!");
#endif
    setOpaque(true);

    //if (g_is_running_as_plugin==true)
    //    setTitleBarHeight(0);

    m_content_comp=jcdp::make_unique<Component>("content");
	m_content_comp->setSize(600, 400);
	setContentOwned(m_content_comp.get(),true);

    m_tool_tip=jcdp::make_unique<TooltipWindow>(this,200);


    m_input_waveform=jcdp::make_unique<WaveFormComponent>(false);
    m_input_waveform->setComponentID("waveform1");
    m_input_waveform->CutFileFunc=[this](time_range tr)
    {
		readbg() << "cut functionality not implemented in this version\n";
		return;
		/*
		auto cut_result=cut_file(m_in_fn,tr,true);
        remove_file_if_exists(m_last_cut_fn);
        if (cut_result.first.isEmpty()==false)
        {
            m_last_cut_fn=cut_result.first;
            double filelen=get_audio_source_info(cut_result.first).get_length_seconds();
            set_input_file(cut_result.first,nullptr,time_range(0.0,filelen));
        }
		*/
    };
	m_input_waveform->EnvelopeChangeRequestedFunc = [this](String envname)
	{
		auto& procinfo = get_current_processor();
		for (int i = 0; i < procinfo.m_parameters.size(); ++i)
		{
			if (procinfo.m_parameters[i].m_name == envname)
			{
				procinfo.m_parameters[i].m_automation_enabled = true;
				m_input_waveform->set_parameter(&procinfo.m_parameters[i]);
				m_param_comps[i]->update_from_param();
				break;
			}
		}
	};
	m_positioner.add(m_input_waveform.get(), [this]()
	{
		int wave_top = 25;
		if (m_large_envelope==false)
			return my_rectangle(5, wave_top, getWidth() - 5, wave_top + 200);
		else 
			return my_rectangle(5, wave_top, getWidth() - 5, getHeight()-70);
	});
	
    m_input_waveform->addChangeListener(this);
    m_content_comp->addAndMakeVisible(m_input_waveform.get());

    m_inwf_scroller=jcdp::make_unique<zoom_scrollbar>();
    m_inwf_scroller->RangeChanged=[this](double t0, double t1)
    { m_input_waveform->set_view_range(t0,t1); };
    m_content_comp->addAndMakeVisible(m_inwf_scroller.get());
    m_positioner.add(m_inwf_scroller.get(),[this]()
    {
        int scroller_top=m_input_waveform->getBottom();
        return my_rectangle(5,scroller_top,getWidth()-5,scroller_top+12);
    });

    m_output_waveform=jcdp::make_unique<WaveFormComponent>(true);
    m_output_waveform->setComponentID("waveform2");
    m_output_waveform->OnSeekFunc=[this](double pos) { m_audio_delegate->seek(pos); };
    m_output_waveform->FilePositionFunc=[this](){return m_audio_delegate->get_position(); };
    m_positioner.add(m_output_waveform.get(),[this]
    {
        int wave_top=m_inwf_scroller->getBottom()+3;
        return my_rectangle(5,wave_top,getWidth()-5,wave_top+50);
    });
    m_content_comp->addAndMakeVisible(m_output_waveform.get());

    m_proc_listbox=jcdp::make_unique<CDPListBox>(m_proc_infos);
    m_positioner.add(m_proc_listbox.get(),[this]()
    {
        int width_th=m_preview_volume_slider->getRight()+5;
        if (getWidth()-200<width_th)
        {
            m_proc_listbox->setVisible(false);
            return my_rectangle(  getWidth()-3,
                                  m_output_waveform->getBottom()+5,
                                  getWidth()-1,
                                  getHeight()-10);
        } else
        {
            m_proc_listbox->setVisible(true);
            return my_rectangle(  getWidth()-300,
                                  m_output_waveform->getBottom()+5,
                                  getWidth()-5,
                                  getHeight()-10);
        }
    } );
    m_proc_listbox->setComponentID("proclist");
    m_proc_listbox->addChangeListener(this);
    m_content_comp->addAndMakeVisible(m_proc_listbox.get());

    m_ok_button=jcdp::make_unique<TextButton>("Render");
    m_ok_button->addListener(this);
    m_ok_button->setComponentID("okbut");
    m_content_comp->addAndMakeVisible(m_ok_button.get());

    m_preview_button=jcdp::make_unique<TextButton>("Preview");
    m_preview_button->addListener(this);
    m_preview_button->setComponentID("previewbut");
    m_content_comp->addAndMakeVisible(m_preview_button.get());

    m_menu_button=jcdp::make_unique<TextButton>("Settings");
    m_menu_button->addListener(this);
    m_content_comp->addAndMakeVisible(m_menu_button.get());

    m_import_button=jcdp::make_unique<TextButton>("Import selected item");
	if (g_is_running_as_plugin == false)
		m_import_button->setButtonText("Import file...");
	m_import_button->addListener(this);
    m_content_comp->addAndMakeVisible(m_import_button.get());

    m_preview_volume_slider=jcdp::make_unique<Slider>(Slider::RotaryVerticalDrag,Slider::NoTextBox);
    m_preview_volume_slider->setPopupDisplayEnabled(true,this,this);
    m_preview_volume_slider->setRange(-12.0,12.0,0.1);
    m_preview_volume_slider->setTextValueSuffix(" dB");
    m_preview_volume_slider->setValue(0.0);
    m_preview_volume_slider->addListener(this);
    m_content_comp->addAndMakeVisible(m_preview_volume_slider.get());

    m_edit_waveform_button=jcdp::make_unique<TextButton>("Waveform");
    m_edit_waveform_button->addListener(this);
    m_edit_waveform_button->setBounds(2,2,60,20);
    m_content_comp->addAndMakeVisible(m_edit_waveform_button.get());

    m_edit_envelope_button=jcdp::make_unique<TextButton>("Envelope");
    m_edit_envelope_button->addListener(this);
    m_edit_envelope_button->setBounds(64,2,60,20);
    m_content_comp->addAndMakeVisible(m_edit_envelope_button.get());

    update_edit_mode_buttons();

    m_status_label=jcdp::make_unique<Label>();
    m_status_label->setBounds(130,2,300,20);
	m_status_label->setColour(Label::textColourId, Colours::white);
    m_content_comp->addAndMakeVisible(m_status_label.get());
    
	m_auto_render_status_label = jcdp::make_unique<Label>();
	m_content_comp->addAndMakeVisible(m_auto_render_status_label.get());
	update_status_label();

    //setUsingNativeTitleBar(true);

    const int button_width=80;
    m_positioner.add(m_ok_button.get(),[this,button_width]()
    {   return my_rectangle(5,
                            m_content_comp->getHeight()-25,
                            button_width,
                            m_content_comp->getHeight()-5); } );
    m_positioner.add(m_preview_button.get(),[this,button_width]()
    {
        return my_rectangle(m_ok_button->getRight()+5,
                            m_content_comp->getHeight()-25,
                            m_ok_button->getRight()+5+button_width,
                            m_content_comp->getHeight()-5);
    });
    m_positioner.add(m_import_button.get(),[this,button_width]()
    {
        return my_rectangle(m_preview_button->getRight()+5,
                            m_content_comp->getHeight()-25,
                            m_preview_button->getRight()+5+120,
                            m_content_comp->getHeight()-5);
    });

    m_positioner.add(m_menu_button.get(),[this,button_width]()
    {
        int menubutleft=m_import_button->getRight()+5;
        if (m_import_button->isVisible()==false)
            menubutleft=m_preview_button->getRight()+5;
        return my_rectangle(menubutleft,
                            m_content_comp->getHeight()-25,
                            menubutleft+button_width,
                            m_content_comp->getHeight()-5);

    });

    m_positioner.add(m_preview_volume_slider.get(),[this]()
    {
        int voldialleft=m_menu_button->getRight()+5;
        return my_rectangle(voldialleft,
                            m_content_comp->getHeight()-30,
                            voldialleft+30,
                            m_content_comp->getHeight()-0);
    });
    
	m_positioner.add(m_auto_render_status_label.get(), [this]()
	{
		int voldialright = m_preview_volume_slider->getRight();
		int proclistleft = m_proc_listbox->getX();
		return my_rectangle(voldialright+5,
			m_content_comp->getHeight() - 30,
			proclistleft-5,
			m_content_comp->getHeight() - 0);
	});

    m_presets_combo=jcdp::make_unique<ComboBox>();
    m_positioner.add(m_presets_combo.get(),[this]()
    {
        return my_rectangle(m_status_label->getRight()+5,
                            2,
                            m_content_comp->getRight()-50,
                            22);
    }
    );
	populate_presets_combo(false);
    m_content_comp->addAndMakeVisible(m_presets_combo.get());
    
    m_presets_combo->addListener(this);
    
	m_presets_button = jcdp::make_unique<TextButton>("...");
	m_positioner.add(m_presets_button.get(), [this]()
	{
		return my_rectangle(m_presets_combo->getRight() + 5,
			2,
			m_content_comp->getRight() - 5,
			22);
	});
	m_presets_button->addListener(this);
	m_content_comp->addAndMakeVisible(m_presets_button.get());

	
    m_proc_listbox->selectRow(index_of_named_processor("Modify Brassage Brassage"));
    init_param_components();
    if (g_is_running_as_plugin==false)
        setSize(800,600);
    //m_commands.add_command("Foo",[]() { Logger::writeToLog("FOO!!!!"); },KeyPress(KeyPress::F1Key));
	load_state();
	load_presets_file();
}

struct presets_combo_comparator
{
	int compareElements(const ValueTree& lhs, const ValueTree& rhs)
	{
		String lhs_n = lhs.getProperty("processorname").toString();
		String rhs_n = rhs.getProperty("processorname").toString();
		if (lhs_n == rhs_n)
			return 0;
		if (lhs_n < rhs_n)
			return -1;
		return 1;
	}
};

void cdp_main_dialog::populate_presets_combo(bool keep_current)
{
	int current_index = m_presets_combo->getSelectedItemIndex();
	m_presets_combo->clear(dontSendNotification);
	//presets_combo_comparator comparator;
	//m_presets_value_tree.sort(comparator, nullptr, true);
	for (int i = 0; i < m_presets_value_tree.getNumChildren(); ++i)
	{
		ValueTree presetvt = m_presets_value_tree.getChild(i);
		String presetname = presetvt.getProperty("presetname").toString();
		String procname = presetvt.getProperty("processorname").toString();
		int preset_index = presetvt.getProperty("index");
		m_presets_combo->addItem(procname+" : "+presetname, preset_index);
	}
	if (keep_current==true)
		m_presets_combo->setSelectedItemIndex(current_index, dontSendNotification);
}

cdp_main_dialog::~cdp_main_dialog()
{
	delete m_plugin_instance;
}

time_range cdp_main_dialog::get_input_time_range() const
{
    return m_input_waveform->get_time_range();
}

void cdp_main_dialog::update_status_label()
{
	m_auto_render_status_label->setColour(Label::textColourId,Colours::white);
    if (m_render_timer_enabled==true)
		m_auto_render_status_label->setText("Auto render enabled",dontSendNotification);
    else m_auto_render_status_label->setText("Auto render disabled",dontSendNotification);
}

void cdp_main_dialog::comboBoxChanged(ComboBox *cb)
{
	if (cb == m_presets_combo.get())
	{
		String selindex = String(cb->getSelectedId());
		String fullpresetname = cb->getText();
		ValueTree presetvt = m_presets_value_tree.getChildWithName(selindex);
		if (presetvt.isValid() == true)
		{
			//readbg() << "preset found from value tree\n";
			String presetprocname = presetvt.getProperty("processorname").toString();
			auto& op = get_current_processor();
			if (op.m_title != presetprocname)
			{
				m_proc_listbox->selectRow(index_of_named_processor(presetprocname));
				init_param_components();
			}
			auto& p = get_current_processor();
			if (p.m_title == presetvt.getProperty("processorname").toString())
			{
				bool param_did_change = deserialize_from_value_tree(p,presetvt);
				if (param_did_change==true)
				{
					m_input_waveform->repaint();
					for (int i = 0; i < m_param_comps.size(); ++i)
						m_param_comps[i]->update_from_param();
					process_deferred(1);
				}
			}
		}
	}
}

void cdp_main_dialog::save_state()
{
	return;
	ValueTree vt("processor_states");
    for (auto& e : *m_proc_infos)
    {
		if (e.m_is_dirty == false)
			continue;
		ValueTree pt(make_valid_id_string(e.m_title));
        ValueTree parstree("parameters");
        pt.addChild(parstree,-1,nullptr);
        for (auto& par : e.m_parameters)
        {
            ValueTree partree(make_valid_id_string(par.m_name));
            partree.setProperty("value",par.m_current_value,nullptr);
            partree.setProperty("env_enabled",par.m_automation_enabled,nullptr);
            ValueTree envpts_tree("envelope_points");
            for (int i=0;i<par.m_env.GetNumNodes();++i)
            {
                const envelope_node& env_node=par.m_env.GetNodeAtIndex(i);
                ValueTree envpt_tree("point");
                envpt_tree.setProperty("time",env_node.Time,nullptr);
                envpt_tree.setProperty("value",env_node.Value,nullptr);
                envpt_tree.setProperty("p1",env_node.ShapeParam1,nullptr);
                envpts_tree.addChild(envpt_tree,-1,nullptr);
            }
            partree.addChild(envpts_tree,-1,nullptr);
            parstree.addChild(partree,-1,nullptr);
        }
        vt.addChild(pt,-1,nullptr);
    }
    XmlElement* xml=vt.createXml();
    g_propsfile->setValue("processor_states",xml);
    delete xml;
}

void cdp_main_dialog::load_state()
{
    XmlElement* xml=g_propsfile->getXmlValue("processor_states");
    if (xml==nullptr)
    {
        return;
    }
    ValueTree vt=ValueTree::fromXml(*xml);
    delete xml;
    std::map<String,parameter_info*> current_params;
    std::map<String,ValueTree> stored_params;
	std::map<parameter_info*, CDP_processor_info*> procparmap;
	for (int i=0;i<vt.getNumChildren();++i)
    {
        ValueTree pt=vt.getChild(i);
        ValueTree parameters=pt.getChildWithName("parameters");
        for (int j=0;j<parameters.getNumChildren();++j)
        {
            ValueTree parameter=parameters.getChild(j);
            String key=pt.getType().toString()+parameter.getType().toString();
            stored_params[key]=parameter;
        }
    }
    for (auto& proc : *m_proc_infos)
        for (auto& param : proc.m_parameters)
        {
            String key=make_valid_id_string(proc.m_title)+make_valid_id_string(param.m_name);
            current_params[key]=&param;
			procparmap[&param] = &proc;
        }
    for (auto& curpar : current_params)
    {
        auto iter=stored_params.find(curpar.first);
        if (iter!=stored_params.end())
        {
			procparmap[curpar.second]->m_is_dirty = true;
			double value=iter->second.getProperty("value");
            curpar.second->m_current_value=value;
			ValueTree envtree = iter->second.getChildWithName("envelope_points");
			if (envtree.isValid() == true)
			{
				bool env_enabled = iter->second.getProperty("env_enabled");
				curpar.second->m_automation_enabled = env_enabled;
				curpar.second->m_env.ClearAllNodes();
				curpar.second->m_env.BeginUpdate();
				for (int i = 0; i < envtree.getNumChildren(); ++i)
				{
					ValueTree envpt_tree = envtree.getChild(i);
					double time = envpt_tree.getProperty("time");
					double value = envpt_tree.getProperty("value");
					double p1 = envpt_tree.getProperty("p1");
					curpar.second->m_env.AddNode(envelope_node(time, value, p1));
				}
				curpar.second->m_env.EndUpdate();
				
			}
        }
    }
	m_input_waveform->repaint();
    for (int i=0;i<m_param_comps.size();++i)
        m_param_comps[i]->update_from_param();
}

void cdp_main_dialog::update_status_label_async(String txt)
{
    MessageManager::callAsync([txt,this](){ m_status_label->setText(txt,dontSendNotification); });
}

void cdp_main_dialog::resized()
{
    ResizableWindow::resized();
    m_positioner.execute();
}

template<typename T>
std::unordered_set<Component*> set_from_up_vector(const std::vector<std::unique_ptr<T>>& vec)
{
    std::unordered_set<Component*> result;
    for (auto &e : vec)
        result.insert(e.get());
    return result;
}
#ifdef CDP_VST_ENABLED
void cdp_main_dialog::init_plugin()
{
	delete m_plugin_instance;
	m_plugin_instance = nullptr;
	m_param_comps.clear();
	CDP_processor_info& the_proc_info = get_current_processor();
	VSTPluginFormat vstformat;
	PluginDescription* desc = m_kplist->getType(the_proc_info.m_plugin_id);
	AudioPluginInstance* pluginst = vstformat.createInstanceFromDescription(*desc,44100.0,512);
	if (pluginst != nullptr)
	{
		if (m_plugin_instance != nullptr && m_plugin_instance->getActiveEditor()!=nullptr)
		{
			//m_content_comp->removeChildComponent(m_plugin_instance->getActiveEditor());
			//m_plugin_instance->
			//delete m_plugin_instance->getActiveEditor();
			
		}
		
		Logger::writeToLog("Managed to create plugin " + pluginst->getName());
		if (pluginst->hasEditor() == true)
		{
			AudioProcessorEditor* ed = pluginst->createEditor();
			m_content_comp->addChildComponent(ed);
			ed->setTopLeftPosition(2, m_output_waveform->getBottom());
		}
		else
		{
			GenericAudioProcessorEditor* ed = new GenericAudioProcessorEditor(pluginst);
			m_content_comp->addChildComponent(ed);
			ed->setVisible(true);
			ed->setSize(300, 200);
			ed->setTopLeftPosition(2, m_output_waveform->getBottom());
		}
		
		//if (m_plugin_instance!=nullptr)
		//	delete m_plugin_instance->getActiveEditor();
		
		m_plugin_instance = pluginst;
	}
	else
		Logger::writeToLog("Could not create plugin");
}
#endif

void cdp_main_dialog::init_param_components()
{
    CDP_processor_info& the_proc_info=get_current_processor();
    int foo=m_proc_listbox->getSelectedRow();
    if (m_current_processor_index==foo)
        return;
    m_current_processor_index=foo;
    m_mono_accepted=false;
    m_state_dirty=true;
	for (int i = 0; i<m_param_comps.size(); ++i)
	{
		m_positioner.remove(m_param_comps[i].get());
	}
	if (the_proc_info.m_main_program == "vstplugin")
	{
#ifdef CDP_VST_ENABLED
		Logger::writeToLog("It's a plugin");
		init_plugin();
#endif
		return;
	}
	// this probably shouldn't depend on a detail like this
    if (the_proc_info.m_sub_program=="stack")
        m_input_waveform->set_show_handle(true);
    else m_input_waveform->set_show_handle(false);
    m_input_waveform->set_parameter(nullptr);
    
    int numparams=the_proc_info.m_parameters.size();
    m_param_comps.resize(numparams);
	StringArray paramnames;
	for (int i=0;i<numparams;++i)
    {
        parameter_info& par=the_proc_info.m_parameters[i];
		if (par.m_can_automate==true)
			paramnames.add(par.m_name);
		m_param_comps[i]=jcdp::make_unique<parameter_component>(&par);
		m_param_comps[i]->StateChangedFunc = [this]() { process_deferred(1); };
		m_param_comps[i]->ShowEnvelopeFunc=[this](parameter_info* parinfo)
        {
            m_input_waveform->set_parameter(parinfo);
        };
        m_param_comps[i]->EnableEnvelopeFunc=[this](parameter_info* parinfo)
        {
            m_input_waveform->repaint();
            process_deferred(1);
        };
        m_positioner.add(m_param_comps[i].get(),[this,i]()
        {
            int avail_horz=m_proc_listbox->getX()-10;
            if (getHeight()>500)
            {
                int lab_top=m_output_waveform->getBottom()+5+i*25;
                return my_rectangle(5,lab_top,avail_horz,lab_top+15);
            } else
            {
                int grid_x=i % 2;
                int grid_y=i / 2;
                int lab_top=m_output_waveform->getBottom()+5+grid_y*25;
                int lab_left=5+avail_horz/2*grid_x;
                return my_rectangle(lab_left,lab_top,lab_left+avail_horz/2,lab_top+15);
            }
        });
        m_content_comp->addAndMakeVisible(m_param_comps[i].get());
        par.m_slider_shaping_func=[this,i](double x)
		{ return m_param_comps[i]->get_value_from_normalized(x); };
        if (par.m_can_automate==true && par.m_env.GetNumNodes()==0)
        {
            par.m_env.AddNode({ 0.0,m_param_comps[i]->get_value_normalized() });
            par.m_env.AddNode({ 1.0,m_param_comps[i]->get_value_normalized() });
        }
		m_param_comps[i]->add_slider_listener(this);
    }
	m_input_waveform->set_parameter_names(paramnames);
	m_positioner.execute();
    m_state_dirty=true;
    process_cdp();
}

void cdp_main_dialog::change_waveform_listener_params()
{
    CDP_processor_info& info=get_current_processor();
    for (auto& e : info.m_parameters)
    {
        if (e.m_notifs==parameter_info::waveformmarker)
        {
            auto source_info=get_audio_source_info_cached(m_in_fn);
            auto tr=m_input_waveform->get_time_range();
            e.m_current_value=(source_info.get_length_seconds()*m_input_waveform->m_handle_pos)-tr.start();
            m_state_dirty=true;
        }
    }
}

void cdp_main_dialog::changeListenerCallback(ChangeBroadcaster *bc)
{
    if (bc==m_proc_listbox.get())
    {
        init_param_components();
        //process_cdp();
    }
    if (bc==m_input_waveform.get())
    {
        change_waveform_listener_params();
        if (m_input_waveform->get_time_range().isValid()==true)
        {
            m_custom_time_set=true;
            m_state_dirty=true;
            //Logger::writeToLog(String::formatted("%f %f",m_input_time_range.start(),m_input_time_range.end()));
        }
        if (m_input_waveform->is_dirty()==true)
            m_state_dirty=true;
        process_deferred(50);
        //process_cdp();
    }
    /*
    for (int i=0;i<m_param_comps.size();++i)
    {
        if (m_param_comps[i].get()==bc)
        {
            if (m_param_comps[i]->m_param->m_automation_enabled==true)
                m_input_waveform->set_envelope(&m_param_comps[i]->m_param->m_env);
            else
                m_input_waveform->set_envelope(nullptr);
            process_deferred(500);
            break;
        }
    }
    */
}



String get_audio_render_path()
{
    if (g_is_running_as_plugin==true)
    {
        char buf[4096];
        GetProjectPath(buf,4096);
        if (strlen(buf)>0)
            return String(buf);
    } else
    {
        if (g_stand_alone_render_dir.exists()==true)
            return g_stand_alone_render_dir.getFullPathName();
        else
        {
            choose_rendering_location();
            return g_stand_alone_render_dir.getFullPathName();
        }
    }
    return String();
}

String get_temp_audio_file_name(String suffix)
{
    return get_audio_render_path()+"/"+c_file_prefix+String(Time::getHighResolutionTicks())+"."+suffix;
}
#ifndef BUILD_CDP_FRONTEND_PLUGIN
void cdp_main_dialog::set_input_file(String fn, MediaItem_Take* take, time_range trange)
{
    if (m_finalized_files.count(fn)>0)
    {
        Logger::writeToLog("file alredy rendered "+fn);
        //return;
    }
    m_in_fn=fn;
	
	if (g_is_running_as_plugin == true)
	{
		m_reaper_take = take;
		auto proc_result = pre_process_file_with_reaper_api(m_reaper_take, time_range(), 1.0, false);
		if (proc_result.first.isEmpty() == false)
		{
			m_input_waveform->set_file(proc_result.first);
			m_in_fn = proc_result.first;

		}
	}
    double slen=get_audio_source_info_cached(m_in_fn).get_length_seconds();

    if (g_is_running_as_plugin==false)
    {
        m_input_waveform->set_time_range({0.0,slen});
		m_input_waveform->set_file(m_in_fn);
    } else
    {
        if (trange.start()<0.0)
            trange=time_range(0.0,trange.end());
        if (trange.end()>slen)
            trange=time_range(trange.start(),slen);
        m_input_waveform->set_time_range(trange);
    }
    m_state_dirty=true;
    m_custom_time_set=false;
    //process_cdp();
}
#else
void cdp_main_dialog::set_reaper_take(MediaItem_Take * take)
{
	m_reaper_take = take;
	auto proc_result = pre_process_file_with_reaper_api(m_reaper_take, time_range(), 1.0, false);
	if (proc_result.first.isEmpty() == false)
	{
		m_input_waveform->set_file(proc_result.first);
		m_in_fn = proc_result.first;
		MediaItem* item = GetMediaItemTake_Item(m_reaper_take);
		if (item != nullptr)
		{
			double item_len = GetMediaItemInfo_Value(item, "D_LENGTH");
			m_input_waveform->set_time_range(time_range(0.0,item_len));
		}
		m_state_dirty = true;
		m_custom_time_set = false;
	}
}
#endif

int cdp_main_dialog::index_of_named_processor(const String& name) const
{
    for (int i=0;i<m_proc_infos->size();++i)
        if ((*m_proc_infos)[i].m_title==name)
            return i;
    return -1;
}

void cdp_main_dialog::closeButtonPressed()
{
    if (g_is_running_as_plugin==false)
        JUCEApplication::quit();
    else
    {
        setVisible(false);
        g_propsfile->setValue("windowvisible",false);
    }
}

void cdp_main_dialog::set_auto_render_enabled(bool b)
{
    m_render_timer_enabled=b;
    g_propsfile->setValue("autorender",m_render_timer_enabled);
    if (m_render_timer_enabled==true)
        process_deferred(1);
    update_status_label();
}

bool cdp_main_dialog::handle_processing_cancel(file_cleaner &cleaner, StringArray &filenames)
{
    /*
    if (m_task_counter>task_counter)
    {
        cleaner.add_multiple(filenames);
        Logger::writeToLog("cancelling task "+String(task_counter)+" after main CDP processing");
        return true;
    }
    */
    return false;
}

void cdp_main_dialog::update_envelope_size()
{
	m_large_envelope = !m_large_envelope;
	m_output_waveform->setVisible(!m_large_envelope);
	m_positioner.execute();
}

bool cdp_main_dialog::keyPressed(const KeyPress &press)
{
    if (g_is_running_as_plugin==true && press==KeyPress::leftKey)
    {
        if (CountSelectedMediaItems(nullptr)==1)
            import_item();
        return true;
    }
    if (press==KeyPress::homeKey)
    {
        set_auto_render_enabled(!m_render_timer_enabled);
        return true;
    }
	if (press == KeyPress::pageDownKey)
	{
		update_envelope_size();
		return true;
	}
    return false;
}

void cdp_main_dialog::userTriedToCloseWindow()
{
	if (g_is_running_as_plugin == false)
		JUCEApplication::quit();
	setVisible(false);
    g_propsfile->setValue("windowvisible",false);
}
inline int cdp_main_dialog::getDesktopWindowStyleFlags() const
{
	if (g_is_running_as_plugin == true)
        return ComponentPeer::windowHasCloseButton | ComponentPeer::windowHasTitleBar | ComponentPeer::windowIsResizable|ComponentPeer::windowHasMinimiseButton;
	return ComponentPeer::windowHasCloseButton | ComponentPeer::windowHasTitleBar | ComponentPeer::windowIsResizable | ComponentPeer::windowAppearsOnTaskbar;
}


void cdp_main_dialog::timerCallback(int id)
{
    if (id==0)
    {
        stopTimer(0);
        update_parameters_from_sliders();
        m_state_dirty=true;
        if (m_render_timer_enabled==false)
            return;
        process_cdp();
    }
}

void cdp_main_dialog::process_deferred(int delms)
{
    if (isTimerRunning(0)==true)
        stopTimer(0);
    startTimer(0,delms);
}

void cdp_main_dialog::sliderValueChanged(Slider* slider)
{
    if (slider!=m_preview_volume_slider.get())
    {
        process_deferred(500);
    }
    else
    {
        double gain=exp(slider->getValue()*0.11512925464970228420089957273422);
        m_audio_delegate->set_volume(gain);
    }
}

void cdp_main_dialog::sliderDragEnded(Slider *slider)
{
    return;
    update_parameters_from_sliders();
    m_state_dirty=true;
    process_cdp();
}

void cdp_main_dialog::finalize_output_file()
{
    m_state_dirty=true;
    if (m_out_fn.isEmpty()==true)
        process_cdp();
    if (m_out_fn.isEmpty()==true)
        return;
    File temp1(m_in_fn);
    String outfilename;
    if (g_propsfile->getBoolValue("always_ask_out_fn",false)==false)
    {
		if (g_is_running_as_plugin == false)
		{
			outfilename = get_audio_render_path() + String("/")
				+ temp1.getFileNameWithoutExtension() + "-"
				+ String(Time::currentTimeMillis())
				+ ".wav";
		}
		else
		{
			char* takename = static_cast<char*>(GetSetMediaItemTakeInfo(m_reaper_take, "P_NAME", nullptr));
			if (takename != nullptr)
			{
				outfilename = get_audio_render_path()+String("/")+String(takename)+"-"+String(Time::currentTimeMillis())+".wav";
			} else
				outfilename = get_audio_render_path() + String("/") + String(Time::currentTimeMillis()) + ".wav";
		}
    } else
    {
        FileChooser chooser("Save processed audio file",
                            File(),"*.wav");
        bool result=chooser.browseForFileToSave(true);
        if (result==true)
        {
            File chosenfile=chooser.getResult();
            outfilename=chosenfile.getFullPathName();
        } else
        {
			update_status_label_async("No file saved!");
			return;
        }
    }
    File temp2(m_out_fn);
    if (temp2.copyFileTo(File(outfilename))==false)
    {
		update_status_label_async("Could not copy processed file to destination");
	} else
    {
        if (g_is_running_as_plugin==true)
        {
            bool add_take=g_propsfile->getBoolValue("addrenderednewtake",true);
            if (add_take==true)
            {
                InsertMedia(outfilename.toRawUTF8(),3);
                m_finalized_files.insert(outfilename);
                const CDP_processor_info& proc=get_current_processor();
                bool change_dur=g_propsfile->getBoolValue("adjustitemlength",true);
                if (proc.m_changes_duration==true && change_dur==true)
                    Main_OnCommand(40612,0);
            } else update_status_label_async("File has been rendered to : "+outfilename);
        }
    }
}

void cdp_main_dialog::update_parameters_from_sliders()
{
    CDP_processor_info& proc=get_current_processor();
    for (int i=0;i<proc.m_parameters.size();++i)
    {
        parameter_info& param_info=proc.m_parameters[i];
        if (param_info.m_notifs==parameter_info::none)
            param_info.m_current_value=m_param_comps[i]->get_value();
    }
}

CDP_processor_info &cdp_main_dialog::get_current_processor()
{
    int index=m_proc_listbox->getSelectedRow();
    if (index<0 || index>=m_proc_infos->size())
        index=0;
    return (*m_proc_infos)[index];
}

void cdp_main_dialog::show_menu()
{
    PopupMenu m;
    //m.addItem (6, "Test state save",true,false);
    //m.addItem (8, "Test state load",true,false);
    m.addItem (5, "About...",true,false);
    PopupMenu subm;
    String bufopt=g_propsfile->getValue("cdp_buf_size","1024");
    std::vector<String> bufsizes{"512","1024","4096","16384"};
    int subid=100;
    for (auto& e: bufsizes)
    {
        bool isticked=e==bufopt;
        subm.addItem(subid,e,true,isticked);
        ++subid;
    }
    m.addSubMenu("CDP internal buffer size",subm,true);

    PopupMenu max_wait_menu;
    int max_wait_opt=g_propsfile->getIntValue("cdp_max_wait",15);
    std::vector<int> wait_times{5,10,15,30,60,90,120};
    subid=200;
    for (auto& e: wait_times)
    {
        bool isticked=e==max_wait_opt;
        max_wait_menu.addItem(subid,String(e),true,isticked);
        ++subid;
    }
    m.addSubMenu("CDP max processing wait time (seconds)",max_wait_menu,true);
    
	std::vector<float> gui_scale_factors{ 0.5,0.6f,0.75,0.8f,0.9f,1.0,1.25,1.5 };
	subid = 300;
	PopupMenu gui_scales_menu;
	for (auto& e : gui_scale_factors)
	{
		bool isticked = e == m_gui_scale_factor;
		gui_scales_menu.addItem(subid, String(e, 2), true, isticked);
		++subid;
	}
	m.addSubMenu("GUI scaling", gui_scales_menu, true);

	bool opt3=g_propsfile->getBoolValue("always_ask_out_fn",false);
    m.addItem (4, "Always ask for output file name",true,opt3);
    m.addItem (1, "Choose render folder...",!opt3,false);
    bool opt1=g_propsfile->getBoolValue("addrenderednewtake",true);
    if (g_is_running_as_plugin==true)
        m.addItem (2, "Add rendered file as new take in item",true,opt1);
    bool opt2=g_propsfile->getBoolValue("adjustitemlength",true);
    if (g_is_running_as_plugin==true)
        m.addItem (3, "Adjust item length if processing changes duration",true,opt2);
    m.addItem (7, "Autorender after changing settings",true,m_render_timer_enabled);
	m.addItem(8, "Loop preview playback", true, m_audio_delegate->is_looped());
	const int result = m.show();
    if (result == 0)
    {
        // user dismissed the menu without picking anything
    }
	else if (result == 8)
	{
		m_audio_delegate->set_looped(!m_audio_delegate->is_looped());
		g_propsfile->setValue("looped_preview", m_audio_delegate->is_looped());
	}
	else if (result == 1)
    {
        choose_rendering_location();
    }
    else if (result == 2)
    {
        g_propsfile->setValue("addrenderednewtake",!opt1);
    }
    else if (result == 3)
    {
        g_propsfile->setValue("adjustitemlength",!opt2);
    }
    else if (result == 4)
    {
        g_propsfile->setValue("always_ask_out_fn",!opt3);
    }
    else if (result == 5)
    {
        AlertWindow::showMessageBoxAsync(AlertWindow::InfoIcon,
                                         String("CDP Front-end (")+__DATE__+")",
                                         "(c) 2014-2018 Xenakios Softwares\n http://xenakios.wordpress.com/\n\n"
										 "With thanks to Oli Larkin for the initial OS-X build\nhttp://www.olilarkin.co.uk/"
										 
			,"OK",
                                         this);
    }
    else if (result == 6)
    {
        save_state();
    }
    else if (result == 7)
    {
        set_auto_render_enabled(!m_render_timer_enabled);
    }
    else if (result == 8)
    {
        load_state();
    }
    else if (result>=100 && result<200)
    {
        m_env_bsize=bufsizes[result-100];
        g_propsfile->setValue("cdp_buf_size",m_env_bsize);
#ifdef WIN32
        auto winresult=SetEnvironmentVariableA("CDP_MEMORY_BBSIZE",m_env_bsize.toRawUTF8());
        if (winresult==0)
        {
            AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon,
                                             "Error",
                                             "Could not set environment variable CDP_MEMORY_BBSIZE","OK",
                                             this);
        }
#else
        m_env_bsize=String("CDP_MEMORY_BBSIZE=")+bufsizes[result-100];
        if (putenv((char*)m_env_bsize.toRawUTF8())!=0)
        {
            AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon,
                                             "Error",
                                             "Could not set environment variable CDP_MEMORY_BBSIZE","OK",
                                             this);
        }

#endif
    }
    else if (result>=200 && result<300)
    {
        g_propsfile->setValue("cdp_max_wait",wait_times[result-200]);
        g_max_child_process_wait_time=1000*wait_times[result-200];
    }
	else if (result >= 300 && result < 400)
	{
		m_gui_scale_factor = gui_scale_factors[result - 300];
		g_propsfile->setValue("gui_scale_factor", gui_scale_factors[result-300]);
		setSize(getWidth()-1,getHeight()-1);
		setSize(getWidth()+1, getHeight()+1);
	}
}

template<typename T,typename U>
inline bool operator==(T* lhs, const std::unique_ptr<U>& rhs)
{
	return lhs == rhs.get();
}

void cdp_main_dialog::buttonClicked(Button *but)
{
    if (but==m_ok_button.get())
        finalize_output_file();
    if (but==m_import_button.get())
        import_file();
    if (but==m_preview_button.get())
        toggle_preview();
    if (but==m_menu_button.get())
        show_menu();
    if (but==m_edit_waveform_button.get())
    {
        m_input_waveform->set_edit_mode(WaveFormComponent::em_waveform);
    }
    if (but==m_edit_envelope_button.get())
    {
        m_input_waveform->set_edit_mode(WaveFormComponent::em_envelope);
    }
	if (but == m_presets_button)
	{
		show_presets_menu();
	}
    update_edit_mode_buttons();
}

void cdp_main_dialog::show_presets_menu()
{
	PopupMenu menu;
	menu.addItem(1, "Add preset");
	menu.addItem(2, "Remove current preset");
	menu.addItem(3, "Update current preset");
	menu.addItem(4, "Rename current preset");
	int result=menu.show();
	if (result == 1)
	{
		AlertWindow dlg("CDP Frontend", "New preset name", AlertWindow::QuestionIcon);
		dlg.addTextEditor("Name", "Untitled");
		dlg.addButton("OK", 1,KeyPress(KeyPress::returnKey));
		dlg.addButton("Cancel", 1);
		int dlgresult=dlg.runModalLoop();
		if (dlgresult == 1)
		{
			String preset_name = dlg.getTextEditor("Name")->getText();
			if (preset_name != "Untitled")
			{
				add_preset_from_current_processor_state(preset_name);
			}
			
		}

	}
	String preset_index = String(m_presets_combo->getSelectedId());
	if (result == 2)
	{
		ValueTree presettree = m_presets_value_tree.getChildWithName(preset_index);
		m_presets_value_tree.removeChild(presettree, nullptr);
		populate_presets_combo(false);
		save_presets_file();
	}
	if (result == 3)
	{
		
		auto& p = get_current_processor();
		ValueTree presettree = m_presets_value_tree.getChildWithName(preset_index);
		if (presettree.isValid() == true)
		{
			String presprocname = presettree.getProperty("processorname").toString();
			if (presprocname == p.m_title)
			{
				ValueTree state = serialize_to_value_tree(p, "state");
				presettree.removeChild(presettree.getChildWithName("state"), nullptr);
				presettree.addChild(state, -1, nullptr);
				save_presets_file();
			}
			else
			{
				AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon,
					"CDP Frontend error",
					"The current preset can't be updated with the state of the currently loaded processor", "OK",
					this);
			}
		}
	}
	if (result == 4)
	{
		ValueTree presettree = m_presets_value_tree.getChildWithName(preset_index);
		if (presettree.isValid() == true)
		{
			String oldname = presettree.getProperty("presetname");
			AlertWindow dlg("CDP Frontend", "New preset name", AlertWindow::QuestionIcon);
			dlg.addTextEditor("Name", oldname);
			dlg.addButton("OK", 1, KeyPress(KeyPress::returnKey));
			dlg.addButton("Cancel", 1);
			int dlgresult = dlg.runModalLoop();
			if (dlgresult == 1)
			{
				String preset_name = dlg.getTextEditor("Name")->getText();
				if (preset_name != oldname)
				{
					presettree.setProperty("presetname", preset_name,nullptr);
					populate_presets_combo(true);
					save_presets_file();
				}

			}
			
		}
		
	}
}

int cdp_main_dialog::get_max_preset_id()
{
	int result = 1;
	for (int i = 0; i < m_presets_value_tree.getNumChildren(); ++i)
	{
		int index = m_presets_value_tree.getChild(i).getProperty("index");
		if (index > result)
			result = index;
	}
	return result;
}

void cdp_main_dialog::add_preset_from_current_processor_state(String name)
{
	auto& p = get_current_processor();
	String fullpresetname = p.m_title + " : " + name;
	int preset_index = get_max_preset_id()+1;
	m_presets_combo->addItem(fullpresetname, preset_index);
	m_presets_combo->setSelectedId(preset_index, dontSendNotification);
	String id(preset_index);
	ValueTree preset_vt(id);
	preset_vt.setProperty("processorname", p.m_title,nullptr);
	preset_vt.setProperty("presetname", name, nullptr);
	preset_vt.setProperty("index", preset_index, nullptr);
	ValueTree procstate = serialize_to_value_tree(p, "state");
	preset_vt.addChild(procstate, -1, nullptr);
	/*
	for (int i = 0; i < p.m_parameters.size(); ++i)
	{
		ValueTree paramtree(make_valid_id_string(p.m_parameters[i].m_name));
		paramtree.setProperty("value", p.m_parameters[i].m_current_value, nullptr);
		preset_vt.addChild(paramtree, -1, nullptr);
	}
	*/
	m_presets_value_tree.addChild(preset_vt, -1,nullptr);
	save_presets_file();
	
}

void cdp_main_dialog::update_current_preset()
{

}

void cdp_main_dialog::save_presets_file()
{
	if (g_is_running_as_plugin == false)
		return;
	XmlElement* xml = m_presets_value_tree.createXml();
	String fn = String(GetResourcePath() + String("/jcdp_presets.xml"));
	File presets_txt_file(fn);
	xml->writeToFile(presets_txt_file, String());
	delete xml;
}

void cdp_main_dialog::load_presets_file()
{
	return;
	if (g_is_running_as_plugin == false)
		return;
	String fn = String(GetResourcePath() + String("/jcdp_presets.xml"));
	File presets_txt_file(fn);
	if (presets_txt_file.existsAsFile() == false)
		return;
	XmlDocument xmldoc(presets_txt_file);
	XmlElement* xmlelem = xmldoc.getDocumentElement();
	ValueTree vt = ValueTree::fromXml(*xmlelem);
	if (vt.isValid() == true)
	{
		m_presets_value_tree = vt;
		populate_presets_combo(false);
	}
	else readbg() << "CDP Frontend : invalid presets tree loaded from disk\n";
}

void cdp_main_dialog::init_audio_hw()
{

}

void cdp_main_dialog::toggle_preview()
{
    if (m_audio_delegate==nullptr)
    {
        Logger::writeToLog("Audio delegate null");
        return;
    }
    if (m_audio_delegate->is_playing()==true)
    {
        m_audio_delegate->stop();
        m_preview_button->setButtonText("Preview");
    } else
    {
        if (ModifierKeys::getCurrentModifiers().isCtrlDown()==false)
        {
            process_cdp();
            m_audio_delegate->set_audio_file(m_out_fn);
        } else
            m_audio_delegate->set_audio_file(m_in_fn);
        m_audio_delegate->start();
        m_preview_button->setButtonText("Stop");
    }
}

void cdp_main_dialog::import_item()
{
#ifdef BUILD_CDP_FRONTEND_PLUGIN
	MediaItem* item=GetSelectedMediaItem(0,0);
    MediaItem_Take* take=GetActiveTake(item);
    PCM_source* source=(PCM_source*)GetSetMediaItemTakeInfo(take,"P_SOURCE",nullptr);
    if (source!=nullptr)
    {
        String source_fn(source->GetFileName());
        if (has_supported_media_type(source)==true)
        {
            double source_len=source->GetLength();
            double take_startoffset=*(double*)GetSetMediaItemTakeInfo(take,"D_STARTOFFS",nullptr);
            double item_length=*(double*)GetSetMediaItemInfo(item,"D_LENGTH",nullptr);
            double take_end_offset=take_startoffset+item_length;
            time_range trange(0.0,item_length);
            if (fuzzy_is_zero(take_startoffset) && fuzzy_compare(item_length,source_len))
                trange=time_range();
            set_reaper_take(take);
            process_cdp();
        }
    }
#endif
}

void cdp_main_dialog::import_file()
{
    if (g_is_running_as_plugin==false)
    {
#ifndef BUILD_CDP_FRONTEND_PLUGIN
		String start_dir=g_propsfile->getValue("import_file_start_folder");
        FileChooser chooser("Choose file to process",
                            File(start_dir),"*.wav;*.aif;*.aiff");
        bool result=chooser.browseForFileToOpen();
        if (result==true)
        {
            g_propsfile->setValue("import_file_start_folder",chooser.getResult().getParentDirectory().getFullPathName());
            set_input_file(chooser.getResult().getFullPathName(),nullptr);
            m_state_dirty=true;
            process_cdp();
        }
#endif
    } else
    {
		if (CountSelectedMediaItems(0) == 1)
		{
			import_item();
		}
		return;
		std::set<String> unique_file_names;
        int num_items=CountMediaItems(nullptr);
        for (int i=0;i<num_items;++i)
        {
            MediaItem* item=GetMediaItem(nullptr,i);
            int num_takes=CountTakes(item);
            for (int j=0;j<num_takes;++j)
            {
                PCM_source* src=GetMediaItemTake_Source(GetMediaItemTake(item,j));
                if (src && src->GetFileName()!=nullptr)
                {
                    unique_file_names.insert(String(src->GetFileName()));
                }
            }
        }
        PopupMenu pop;
        // this is soooo stupid, the Juce menu won't allow accessing the item texts later...
        std::vector<String> fn_vec;
        int id=1;
        for (auto& e : unique_file_names)
        {
            fn_vec.push_back(e);
            pop.addItem(id,e,true,false);
            ++id;
            if (id>50)
                break;
        }
        pop.addSeparator();
        pop.addItem(100,"Selected media item",true,false);
        pop.addItem(101,"Follow item selection",true,m_follow_item_selection);
        const int result = pop.show();
        if (is_in_range(result,1,50)==true)
        {
            //set_input_file(fn_vec[result-1],nullptr);
            m_state_dirty=true;
            process_cdp();
        }
        if (result==100 && CountSelectedMediaItems(0)==1)
        {
            import_item();
        }
        if (result==101)
        {
            m_follow_item_selection=!m_follow_item_selection;
            g_propsfile->setValue("follow_item_selection",m_follow_item_selection);
        }
    }
}

std::pair<StringArray, String> cdp_main_dialog::do_pvoc_analysis(StringArray infiles, int wsize, int olap)
{
    child_processes processes;
    StringArray outfilenames;
    for (int i=0;i<infiles.size();++i)
    {
        File temp1(infiles[i]);
        String outfilename=get_audio_render_path()+"/"+c_file_prefix+temp1.getFileNameWithoutExtension()+String(wsize)+String(olap)+".ana";
        outfilenames.add(outfilename);
        remove_file_if_exists(outfilename);
        StringArray pvocargs;
        pvocargs.add(g_cdp_binaries_dir.getFullPathName()+"/pvoc");
        pvocargs.add("anal");
        pvocargs.add("1");
        pvocargs.add(infiles[i]);
        pvocargs.add(outfilename);
        pvocargs.add(String::formatted("-c%d",wsize));
        pvocargs.add(String::formatted("-o%d",olap));
        Logger::writeToLog("Starting ChildProcess pvoc anal "+infiles[i]);
        //Logger::writeToLog(pvocargs.joinIntoString(" "));
        processes.add_and_start_task(pvocargs);
    }
    String r=processes.wait_for_finished(g_max_child_process_wait_time);
    if (r.isEmpty()==true)
    {
        return std::make_pair(outfilenames,String());
    }
    return std::make_pair(StringArray(),r);
}

std::pair<String,String> cdp_main_dialog::cut_file(String infn, time_range trange, bool excise)
{
    if (trange.isValid()==false)
        return std::make_pair(infn,String());
    String outfn=get_temp_audio_file_name();
    remove_file_if_exists(outfn);
    ChildProcess proc;
    StringArray cutargs;
    cutargs.add(g_cdp_binaries_dir.getFullPathName()+"/sfedit");
    if (excise==false)
    {
        cutargs.add("cut");
        cutargs.add("1");
    } else
    {
        cutargs.add("excise");
        cutargs.add("1");
    }
    cutargs.add(infn);
    cutargs.add("-f"+outfn);
    cutargs.add(String(trange.start()));
    cutargs.add(String(trange.end()-0.001)); // the CDP cut program errors out if the end is even slightly past the file end
    proc.start(cutargs);
    proc.waitForProcessToFinish(g_max_child_process_wait_time);
    {
        if (proc.getExitCode()==0)
        {
            return std::make_pair(outfn,String());
        }
    }
    return std::make_pair(String(),proc.readAllProcessOutput());
}

String cdp_main_dialog::adjust_file_volume(String infn, double vol)
{
    String outfn=get_temp_audio_file_name();
    remove_file_if_exists(outfn);
    auto result=run_process({g_cdp_binaries_dir.getFullPathName()+"/modify",
                            "loudness","2",
                            infn,"-f"+outfn,
                            String(vol)},g_max_child_process_wait_time);
    if (result.second==0)
        return outfn;
    return String();
}

String cdp_main_dialog::monoize_file(String infn)
{
    String outfn=get_temp_audio_file_name();
    //String outfn(get_audio_render_path()+"/monoized_"+String(Time::currentTimeMillis())+".wav");
    remove_file_if_exists(outfn);
    ChildProcess proc;
    StringArray monoizer_args;
    monoizer_args.add(g_cdp_binaries_dir.getFullPathName()+"/housekeep");
    monoizer_args.add("chans");
    monoizer_args.add("4");
    monoizer_args.add(infn);
    monoizer_args.add("-f"+outfn);
    proc.start(monoizer_args);
    proc.waitForProcessToFinish(g_max_child_process_wait_time);
    {
        if (proc.getExitCode()==0)
        {
            return outfn;
        }
    }
    return String();
}

std::pair<StringArray, String> cdp_main_dialog::do_pvoc_resynth(StringArray infiles)
{
    bool do_parallel=true;
    child_processes processes;
    StringArray outfiles;
    for (int i=0;i<infiles.size();++i)
    {
        String resynthtoutfn=get_temp_audio_file_name();
        remove_file_if_exists(resynthtoutfn);
        outfiles.add(resynthtoutfn);
        StringArray pvocargs;
        pvocargs.add(g_cdp_binaries_dir.getFullPathName()+"/pvoc");
        pvocargs.add("synth");
        pvocargs.add(infiles[i]);
        pvocargs.add("-f"+resynthtoutfn);
        if (do_parallel==true)
            processes.add_and_start_task(pvocargs);
        else
        {
            processes.add_task(pvocargs);
        }
    }
    String r;
    if (do_parallel==true)
        r=processes.wait_for_finished(g_max_child_process_wait_time);
    else r=processes.process_sequentially(g_max_child_process_wait_time);
    if (r.isEmpty()==true)
        return std::make_pair(outfiles,String());
    return std::make_pair(StringArray(),r);
}

String generate_cmd_argument(parameter_info& param,
                             double inputfilelen,
                             file_cleaner& cleaner,
                             CDP_processor_info& procinfo,time_range time_selection)
{
    if (time_selection.isValid()==false)
        time_selection=time_range(0.0,inputfilelen);
    if (param.m_cmd_arg_formatter)
    {
        auto result=param.m_cmd_arg_formatter(&param);
        if (result.second==true)
            cleaner.add(result.first);
        return result.first;
    }
    if (param.m_automation_enabled==false)
    {
        if (param.m_cmd_prefix.isEmpty()==true)
            return String(param.m_current_value);
        else
            return param.m_cmd_prefix+String(param.m_current_value);
    } else
    {
        String parname=param.m_name.replaceCharacter(' ','_');
        String env_fn=get_audio_render_path()+"/"+c_file_prefix+parname+String(Time::getHighResolutionTicks())+".txt";
        File env_txt_file(env_fn);
        FileOutputStream* os=env_txt_file.createOutputStream();
        if (os!=nullptr)
        {
            double valuerange=param.m_maximum_value-param.m_minimum_value;
            if (time_selection.start()>0.0)
            {
                //Logger::writeToLog(String::formatted("time selection %f %f",time_selection.start(),time_selection.end()));
                double scalednormtime=1.0/inputfilelen*time_selection.start();
                double normalizedvalue=param.m_env.GetInterpolatedNodeValue(scalednormtime);
                double scaledvalue=param.m_minimum_value+valuerange*normalizedvalue;
                (*os) << String::formatted("%f %f\n",0.0,scaledvalue);
                //Logger::writeToLog(String::formatted("\t%f %f",0.0,scaledvalue));
            }
            const int num_sub_segments=15;
            for (int i=0;i<param.m_env.GetNumNodes()-1;++i)
            {
                const envelope_node& node0=param.m_env.GetNodeAtIndex(i);
                const envelope_node& node1=param.m_env.GetNodeAtIndex(i+1);
                double value_delta=node1.Value-node0.Value;
                double time_delta=node1.Time-node0.Time;
                //Logger::writeToLog(String::formatted("env node %d",i));
                for (int j=0;j<num_sub_segments;++j)
                {
                    double to_shaping=1.0/(num_sub_segments)*j;
                    double to_output=time_delta/num_sub_segments*j;
                    double shaped=node0.Value+value_delta*get_shaped_value(to_shaping,0,node0.ShapeParam1,0.0);
                    double scaled=param.m_slider_shaping_func(shaped);
                    double scaledtime;
                    if (param.m_envelope_time_scaling_func)
                    {
                        scaledtime=(node0.Time+to_shaping)*param.m_envelope_time_scaling_func(&procinfo);
                        (*os) << String::formatted("%f %f\n",scaledtime,scaled);
                    }
                    else
                    {
                        scaledtime=node0.Time*inputfilelen+to_output*inputfilelen;
                        if (scaledtime>=time_selection.start() && scaledtime<=time_selection.end())
                        {
                            scaledtime-=time_selection.start();
                            (*os) << String::formatted("%f %f\n",scaledtime,scaled);
                            //Logger::writeToLog(String::formatted("\t%f %f",scaledtime,scaled));
                        }

                    }
                }

            }
            if (time_selection.isValid()==true)
            {
                double scalednormtime=1.0/inputfilelen*time_selection.end();
                double normalizedvalue=param.m_env.GetInterpolatedNodeValue(scalednormtime);
                double scaledvalue=param.m_minimum_value+valuerange*normalizedvalue;
                (*os) << String::formatted("%f %f\n",time_selection.length(),scaledvalue);
                //Logger::writeToLog(String::formatted("\t%f %f",time_selection.length(),scaledvalue));
            }
            delete os;
            cleaner.add(env_fn);
            if (param.m_cmd_prefix.isEmpty()==true)
                return env_fn;
            else return param.m_cmd_prefix+env_fn;
        }
    }
    return String();
}

std::pair<StringArray, String> cdp_main_dialog::split_stereo_file(String fn, file_cleaner &cleaner)
{
    StringArray result;
    // housekeep chans 2 infile
    File helper_file(fn);
    File helper_directory=helper_file.getParentDirectory();
    for (int i=0;i<2;++i)
    {
        String temp_fn=helper_directory.getFullPathName()+"/"+
                helper_file.getFileNameWithoutExtension()+"_c"+String(i+1)+helper_file.getFileExtension();
        remove_file_if_exists(temp_fn,true);
        cleaner.add(temp_fn,true);
        result.add(temp_fn);
    }
    ChildProcess proc;
    StringArray proc_args;
    proc_args.add(g_cdp_binaries_dir.getFullPathName()+"/housekeep");
    proc_args.add("chans");
    proc_args.add("2");
    proc_args.add(fn);
    proc.start(proc_args);
    proc.waitForProcessToFinish(g_max_child_process_wait_time);
    if (proc.getExitCode()==0)
    {
        return std::make_pair(result,String());
    }
    return std::make_pair(StringArray(),proc.readAllProcessOutput());
}

std::pair<String, String> cdp_main_dialog::merge_split_files(StringArray infiles)
{
    //submix interleave sndfile1 sndfile2 [sndfile3 sndfile4] outfile
    String outfn=get_temp_audio_file_name();
    ChildProcess proc;
    StringArray merge_args;
    merge_args.add(g_cdp_binaries_dir.getFullPathName()+"/submix");
    merge_args.add("interleave");
    for (int i=0;i<infiles.size();++i)
    {
        merge_args.add(infiles[i]);
    }
    merge_args.add(outfn);
    proc.start(merge_args);
    proc.waitForProcessToFinish(g_max_child_process_wait_time);
    if (proc.getExitCode()==0)
    {
        return std::make_pair(outfn,String());
    }
    return std::make_pair(String(),proc.readAllProcessOutput());
}

void cdp_main_dialog::focusLost(FocusChangeType reason)
{
	ResizableWindow::focusLost(reason);
	//readbg() << "focus lost\n";
}

void cdp_main_dialog::focusGained(FocusChangeType reason)
{
	ResizableWindow::focusGained(reason);
	//readbg() << "focus gained\n";
}

bool finalize_cdp_process(CDP_processor_info& procinfo,
						  const StringArray& processed_files,
						  String& outfile,
						  file_cleaner& cleaner)
{
	if (processed_files.size() == 0)
		return false;
	String old_out_file = outfile;
	outfile = processed_files[0];
	if (old_out_file.isEmpty() == false)
		cleaner.add(old_out_file);
	return true;
}

void cdp_main_dialog::commit_cdp_render()
{
	MessageManager::callAsync([this]()
	{
		m_output_waveform->set_file(m_out_fn);
		m_state_dirty = false;
		m_is_processing_cdp = false;
		update_status_label_async("CDP process ok!");
		save_state();
	});
}

void cdp_main_dialog::process_cdp()
{
    if (m_is_processing_cdp==true)
    {
        m_task_counter_mutex.lock();
        ++m_task_counter;
        m_task_counter_mutex.unlock();
        Logger::writeToLog("Already processing CDP...");
        //return;
    }
    //run_at_scope_end restore_status([this](){update_status_label();m_is_processing_cdp=false;});
    if (m_state_dirty==false)
        return;
    if (m_in_fn.isEmpty()==true)
        return;
    if (g_cdp_binaries_dir.exists()==false)
    {
        AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon,
                                         "CDP processing error",
                                         "CDP binaries location not set","OK",
                                         this);
        return;
    }
	m_task_counter_mutex.lock();
    int task_counter=m_task_counter;
    m_task_counter_mutex.unlock();
    auto render_task=[this,task_counter]()
    {
        run_at_scope_end restore_status([this]()
        {
            m_is_processing_cdp=false;
            MessageManager::callAsync([this](){ update_status_label();});
        });
        m_is_processing_cdp=true;

        m_output_waveform->m_render_elapsed_time=0.0;
        double bench_t0=Time::getMillisecondCounterHiRes();
        /*
        Thread::sleep(1000);
        if (m_task_counter>task_counter)
        {
            Logger::writeToLog("cancelling task "+String(task_counter)+" after test sleep");
            return;
        }
        */
        CDP_processor_info& the_proc_info=get_current_processor();
		the_proc_info.m_is_dirty = true;
		auto info=get_audio_source_info_cached(m_in_fn);
        String infntouse;
        bool makemono=false;
        if (2==1)
        {
            if (info.num_channels!=1 && (the_proc_info.m_is_spectral==true || the_proc_info.m_mono_only==true))
            {
                if (m_mono_accepted==false)
                {
                    bool do_as_mono=AlertWindow::showOkCancelBox(AlertWindow::WarningIcon,
                                                                 "CDP processing",
                                                                 "This processor only supports mono files.\nDo you want to process in mono?",
                                                                 "Yes","No",
                                                                 this);
                    if (do_as_mono==false)
                        return;
                    m_mono_accepted=true;
                }
                makemono=true;

            }
        }
        makemono=false;
        file_cleaner tempfilecleaner;
        double infilelen=get_audio_source_info_cached(m_in_fn).get_length_seconds();
		if (g_is_running_as_plugin == false)
		{
			auto cut_result = cut_file(m_in_fn, m_input_waveform->get_time_range());
			infntouse = cut_result.first;
			if (infntouse.isEmpty() == true)
			{
				MessageManager::callAsync([this,cut_result]()
				{
					//m_bubble->showAt(m_input_waveform.get(), AttributedString("CDP sfedit cut failed\n" + cut_result.second), 10000);
				});
				infntouse = m_in_fn;
				return;
			}
			else
				update_status_label_async("Cut OK...");
			if (infntouse != m_in_fn)
				tempfilecleaner.add(infntouse);

			if (makemono == true)
			{
				infntouse = monoize_file(infntouse);
				if (infntouse.isEmpty() == true)
				{
					MessageManager::callAsync([this]()
					{
						//m_bubble->showAt(m_input_waveform.get(), AttributedString("CDP stereo to mono failed"), 5000);
					});
					return;
				}
				tempfilecleaner.add(infntouse);
			}
			double prevolume = the_proc_info.m_parameters[0].m_current_value;
			if (fuzzy_is_zero(prevolume) == false)
			{
				infntouse = adjust_file_volume(infntouse, prevolume);
				if (infntouse.isEmpty() == true)
				{
					MessageManager::callAsync([this]()
					{
						//m_bubble->showAt(m_input_waveform.get(), AttributedString("CDP adjust volume failed"), 5000);
					});
					return;
				}
				else
					update_status_label_async("Volume adjust OK...");
				tempfilecleaner.add(infntouse);
			}
		}
		else
		{
			double prevolume = the_proc_info.m_parameters[0].m_current_value;
			double pregain = exp(prevolume*0.11512925464970228420089957273422);
			auto preprocresult = pre_process_file_with_reaper_api(m_reaper_take, m_input_waveform->get_time_range(), pregain, false);
			if (preprocresult.first.isEmpty() == true)
			{
				MessageManager::callAsync([this]()
				{
					update_status_label_async("REAPER AudioAccessor processing failed");
				});
				return;
			}
			infntouse = preprocresult.first;
		}
			
        StringArray infiles;
        if (the_proc_info.m_mono_only==false)
            infiles.add(infntouse);
        else
        {
            if (info.num_channels==1)
                infiles.add(infntouse);
            else
            {
                Logger::writeToLog("Beginning split channels processing...");
                auto split_result=split_stereo_file(infntouse,tempfilecleaner);
                if (split_result.second.isEmpty()==true)
                {
                    infiles.addArray(split_result.first);
                    update_status_label_async("Split file OK...");
                } else
                {
                    MessageManager::callAsync([split_result,this]()
                    {
						m_status_label->setText("CDP channel split failed\n"+split_result.second,dontSendNotification);
					});
                    return;
                }
            }
        }
        if (the_proc_info.m_is_spectral==true)
        {
			int wsize = m_param_comps[1]->get_value();
            int olap=m_param_comps[2]->get_value();
            double pvoc_t0=Time::getMillisecondCounterHiRes();
            auto pvoc_anal_result=do_pvoc_analysis(infiles,wsize,olap);
            double pvoc_t1=Time::getMillisecondCounterHiRes();
            Logger::writeToLog("pvoc analysis took "+String(pvoc_t1-pvoc_t0)+" milliseconds");
            if (pvoc_anal_result.second.isEmpty()==true)
            {
                infiles.clear();
                infiles.addArray(pvoc_anal_result.first);
                tempfilecleaner.add_multiple(pvoc_anal_result.first);
                update_status_label_async("PVOC analysis OK...");
            } else
            {
                MessageManager::callAsync([this,pvoc_anal_result]()
				{
					m_status_label->setText("CDP pvoc analysis failed\n" + pvoc_anal_result.second, dontSendNotification);
				});
                return;
            }

        }
        StringArray outfiles;
        String prog_output;
        child_processes processes;
        bool main_proc_parallel=true;
        double main_proc_t0=Time::getMillisecondCounterHiRes();
        for (int ch=0;ch<infiles.size();++ch)
        {
            StringArray procargs;
            String procoutfilename=get_temp_audio_file_name();
            if (the_proc_info.m_is_spectral==true)
                procoutfilename=get_temp_audio_file_name("ana");
            // if more than 1 infile, we know the files processed here will be temporary
            if (infiles.size()>1)
                tempfilecleaner.add(procoutfilename);
            Logger::writeToLog("processing "+infiles[ch]);
            if (the_proc_info.m_is_spectral==true)
            {
                procoutfilename=get_temp_audio_file_name("ana");
                tempfilecleaner.add(procoutfilename);
            }
            remove_file_if_exists(procoutfilename);
            procargs.add(g_cdp_binaries_dir.getFullPathName()+"/"+the_proc_info.m_main_program);
            procargs.add(the_proc_info.m_sub_program);
            if (the_proc_info.m_mode.isEmpty()==false)
                procargs.add(the_proc_info.m_mode);
            procargs.add(infiles[ch]);
            if (the_proc_info.m_is_spectral==false)
                procargs.add("-f"+procoutfilename);
            else
                procargs.add(procoutfilename);
            int param_index_offset=1;
            if (the_proc_info.m_is_spectral==true)
                param_index_offset=3;
            for (int i=param_index_offset;i<the_proc_info.m_parameters.size();++i)
            {
                parameter_info& paraminfo=the_proc_info.m_parameters[i];
                procargs.add(generate_cmd_argument(paraminfo,
                                                   infilelen,
                                                   tempfilecleaner,
                                                   the_proc_info,
                                                   m_input_waveform->get_time_range()));
            }
            outfiles.add(procoutfilename);
            if (main_proc_parallel==true)
                processes.add_and_start_task(procargs);
            else processes.add_task(procargs);
        }
        if (main_proc_parallel==true)
            prog_output=processes.wait_for_finished(g_max_child_process_wait_time);
        else prog_output=processes.process_sequentially(g_max_child_process_wait_time);
        double main_proc_t1=Time::getMillisecondCounterHiRes();
        m_task_counter_mutex.lock();
        if (m_task_counter>task_counter)
        {
            m_task_counter_mutex.unlock();
            tempfilecleaner.add_multiple(outfiles);
            Logger::writeToLog("cancelling task "+String(task_counter)+" after main CDP processing");
            return;
        }
        m_task_counter_mutex.unlock();
        Logger::writeToLog("main processing took "+String(main_proc_t1-main_proc_t0)+" milliseconds");
        if (prog_output.isEmpty()==true)
        {
            if (do_all_files_exist(outfiles)==false)
            {
                MessageManager::callAsync([this]()
                {
					m_status_label->setText("Error : CDP returned success but file or files were not created",dontSendNotification);
				});
                return;
            }
            update_status_label_async("Main processing OK...");
            if (the_proc_info.m_is_spectral==false)
            {
                double bench_t1=Time::getMillisecondCounterHiRes();
                m_output_waveform->m_render_elapsed_time=(bench_t1-bench_t0)/1000.0;
                if (outfiles.size()==1)
                {
					if (finalize_cdp_process(the_proc_info, outfiles, m_out_fn, tempfilecleaner) == true)
					{
						m_audio_delegate->set_audio_file(m_out_fn);
						commit_cdp_render();
					}
					
                } else
                {
                    //Logger::writeToLog("Split processing done, joining output files...");
					auto merge_result=merge_split_files(outfiles);
                    if (merge_result.second.isEmpty()==true)
                    {
						String old_out_file = m_out_fn;
						m_out_fn = merge_result.first;
						m_audio_delegate->set_audio_file(m_out_fn);
						if (old_out_file.isEmpty() == false)
							tempfilecleaner.add(old_out_file);
						commit_cdp_render();
					} else
                    {
						MessageManager::callAsync([this]()
						{
							m_status_label->setText("Merging files failed",dontSendNotification);
						});
                    }
                    return;
                }
                return;
            } else
            {
                if (outfiles.size()==1)
                {
                    auto resynth_result=do_pvoc_resynth(outfiles);
                    if (resynth_result.second.isEmpty()==true)
                    {
                        double bench_t1=Time::getMillisecondCounterHiRes();
                        m_output_waveform->m_render_elapsed_time=(bench_t1-bench_t0)/1000.0;
						String old_out_file = m_out_fn;
						m_out_fn = resynth_result.first[0];
						m_audio_delegate->set_audio_file(m_out_fn);
						if (old_out_file.isEmpty()==false)
							tempfilecleaner.add(old_out_file);
						commit_cdp_render();
                        return;
                    } else
                    {
                        MessageManager::callAsync([resynth_result,this]()
                        {
							m_status_label->setText("CDP pvoc resynthesis failed\n" + resynth_result.second, dontSendNotification);
						});
                        return;
                    }
                }
                if (outfiles.size()==2)
                {
                    double resynth_t0=Time::getMillisecondCounterHiRes();
                    auto resynth_result=do_pvoc_resynth(outfiles);
                    double resynth_t1=Time::getMillisecondCounterHiRes();
                    Logger::writeToLog("pvoc resynth took "+String(resynth_t1-resynth_t0)+" milliseconds");
                    if (resynth_result.second.isEmpty()==true)
                    {
                        update_status_label_async("PVOC resynth OK...");
                        tempfilecleaner.add_multiple(resynth_result.first);
                        if (resynth_result.first.size()==outfiles.size())
                        {
                            auto merge_result=merge_split_files(resynth_result.first);
                            if (merge_result.second.isEmpty()==true)
                            {
                                double bench_t1=Time::getMillisecondCounterHiRes();
                                m_output_waveform->m_render_elapsed_time=(bench_t1-bench_t0)/1000.0;
								String old_out_file = m_out_fn;
								m_out_fn = merge_result.first;
								m_audio_delegate->set_audio_file(m_out_fn);
								if (old_out_file.isEmpty() == false)
									tempfilecleaner.add(old_out_file);
								commit_cdp_render();
                                return;
                            } else
                            {
                                MessageManager::callAsync([this]()
                                {
									m_status_label->setText("CDP file merge failed",dontSendNotification);
								});
                                return;
                            }
                        }
                    } else
                    {
                        MessageManager::callAsync([resynth_result,this]()
                        {
							m_status_label->setText("CDP pvoc resynthesis failed\n" + resynth_result.second, dontSendNotification);
						});
                        return;
                    }
                }

            }
        }
        if (prog_output.length()>1024)
            prog_output="Error output too long to show";
        MessageManager::callAsync([this,prog_output]()
        {
            AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon,
                                             "CDP processing error",
                                             prog_output,"OK",
                                             this);
        });
    };
	render_task();
    //std::async(std::launch::async,render_task);


}

void cdp_main_dialog::update_edit_mode_buttons()
{
    if (m_input_waveform->get_edit_mode()==WaveFormComponent::em_waveform)
    {
        m_edit_waveform_button->setAlpha(1.0);
        m_edit_envelope_button->setAlpha(0.5);
    }
    if (m_input_waveform->get_edit_mode()==WaveFormComponent::em_envelope)
    {
        m_edit_waveform_button->setAlpha(0.5);
        m_edit_envelope_button->setAlpha(1.0);
    }
}

CDPListBox::CDPListBox(std::vector<CDP_processor_info> *infos) :
    m_infos(infos)
{
    setModel(this);
    setColour(backgroundColourId,Colours::grey);
    setRowHeight(20);
}

void CDPListBox::paintListBoxItem(int rowNumber, Graphics &g, int width, int height, bool rowIsSelected)
{
    if (rowNumber<0 || rowNumber>=m_infos->size())
        return;
    if (rowIsSelected)
        g.fillAll (Colours::lightblue);
    const CDP_processor_info& pinfo=(*m_infos)[rowNumber];
    if (pinfo.m_is_spectral==false)
        g.setColour (Colours::white);
    else g.setColour(Colours::blanchedalmond);
    g.setFont (height * 0.7f);
    g.drawText(pinfo.m_title,0,0,width,height,Justification::centredLeft,true);
}

void CDPListBox::listBoxItemClicked(int row, const MouseEvent &e)
{
    sendChangeMessage();
}

void CDPListBox::selectedRowsChanged(int lastRowSelected)
{
    sendChangeMessage();
}

StringArray combo_items_from_name(String s)
{
	StringArray result;
	if (s == "FFT Size")
	{
		result = make_string_array({ "128","256","512","1024","2048","4096" });
	}
	if (s == "FFT Overlap")
	{
		result = make_string_array({ "1","2","3","4" });
	}
	return result;
}

void parameter_component::update_combo_item_index()
{
	for (int i = 0; i < m_combo->getNumItems(); ++i)
		if (m_combo->getItemText(i) == String(m_param->m_current_value))
		{
			m_combo->setSelectedItemIndex(i, dontSendNotification);
			break;
		}
}

parameter_component::parameter_component(parameter_info *ppar) : m_param(ppar)
{
    m_name_label=jcdp::make_unique<Label>();
    m_name_label->setText(m_param->m_name,dontSendNotification);
    m_name_label->setColour(Label::textColourId,Colours::white);
    m_name_label->setBounds(1,1,100,15);
    addAndMakeVisible(m_name_label.get());
	if (m_param->m_name == "FFT Size" || m_param->m_name=="FFT Overlap")
	{
		m_combo = jcdp::make_unique<ComboBox>();
		m_combo->addItemList(combo_items_from_name(m_param->m_name),1);
		update_combo_item_index();
		
		m_combo->addListener(this);
		addAndMakeVisible(m_combo.get());
	}
	else
	{
		m_param_slider = jcdp::make_unique<Slider>(Slider::LinearHorizontal, Slider::TextBoxRight);
		m_param_slider->setRange(m_param->m_minimum_value, m_param->m_maximum_value, m_param->m_step);
		m_param_slider->setValue(m_param->m_current_value, dontSendNotification);
		m_param_slider->setDoubleClickReturnValue(true, m_param->m_default_value);
		if (m_param->m_skewed == true)
			m_param_slider->setSkewFactor(m_param->m_skew);
		addAndMakeVisible(m_param_slider.get());
	}
    m_enable_autom_button=jcdp::make_unique<TextButton>("Off");
    m_enable_autom_button->setColour(TextButton::buttonColourId,Colour::fromString("ffbbbbff"));
    addAndMakeVisible(m_enable_autom_button.get());
    m_enable_autom_button->addListener(this);
    m_show_autom_button=jcdp::make_unique<TextButton>("Show");
    m_show_autom_button->addListener(this);
    addAndMakeVisible(m_show_autom_button.get());
    if (m_param->m_can_automate==false)
    {
        m_enable_autom_button->setVisible(false);
        m_show_autom_button->setVisible(false);
    }
}

void parameter_component::resized()
{
    m_enable_autom_button->setBounds(getWidth()-70,0,30,getHeight());
    m_show_autom_button->setBounds(getWidth()-35,0,30,getHeight());
    if (m_param_slider!=nullptr)
		m_param_slider->setBounds(m_name_label->getRight()+5,
								  0,
								  getWidth()-180,
								  getHeight());
	if (m_combo != nullptr)
		m_combo->setBounds(m_name_label->getRight() + 15, 0, 50, getHeight());
}

void parameter_component::buttonClicked(Button *but)
{
    if (but==m_enable_autom_button.get())
    {
        m_param->m_automation_enabled=!m_param->m_automation_enabled;
		update_from_param();
		safe_call(EnableEnvelopeFunc, m_param);
		
    }
    if (but==m_show_autom_button.get())
        safe_call(ShowEnvelopeFunc,m_param);
}

void parameter_component::update_from_param()
{
	if (m_param_slider != nullptr)
	{
		m_param_slider->setValue(m_param->m_current_value, dontSendNotification);

		if (m_param->m_automation_enabled == true)
		{
			m_enable_autom_button->setColour(TextButton::buttonColourId, Colours::lightgreen);
			m_param_slider->setAlpha(0.5);
			m_enable_autom_button->setButtonText("On");
		}
		else
		{
			m_enable_autom_button->setColour(TextButton::buttonColourId, Colour::fromString("ffbbbbff"));
			m_param_slider->setAlpha(1.0);
			m_enable_autom_button->setButtonText("Off");
		}
	}
	if (m_combo != nullptr)
	{
		update_combo_item_index();
	}
}

double parameter_component::get_value() const
{
	if (m_param_slider!=nullptr)
		return m_param_slider->getValue();
	return m_combo->getText().getIntValue();
}

double parameter_component::get_value_normalized() const
{
	if (m_param_slider!=nullptr)
		return m_param_slider->valueToProportionOfLength(m_param_slider->getValue());
	return 1.0/(m_combo->getNumItems()+1)*m_combo->getSelectedItemIndex();
}

double parameter_component::get_value_from_normalized(double x) const
{
	if (m_param_slider!=nullptr)
		return m_param_slider->proportionOfLengthToValue(x);
	int index = (m_combo->getNumItems() - 1)*x;
	return m_combo->getItemText(index).getIntValue();
}

void parameter_component::add_slider_listener(Slider::Listener* listener)
{
	if (m_param_slider!=nullptr)
		m_param_slider->addListener(listener);
}

void parameter_component::comboBoxChanged(ComboBox* combo)
{
	m_param->m_current_value = m_combo->getText().getIntValue();
	safe_call(StateChangedFunc);
}