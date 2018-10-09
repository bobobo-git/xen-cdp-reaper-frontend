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

#include <memory>
//#include "C:/ProgrammingProjects/Projects/reaper-cdp-frontend-extension-plugin/cdp_frontend/JuceLibraryCode/AppConfig.h"
#include "jcdp_main_dialog.h"
#include "JuceHeader.h"

#ifndef WIN32
#include "stdlib.h"
#include "swell-internal.h"
#endif

//#define DOCKED_WINDOW

#define REAPER_PLUGIN_FUNCTIONS_IMPL_LOADFUNC
#define REAPERAPI_DECL
#include "reaper_plugin_functions.h"

#undef min
#undef max

#include "jcdp_utilities.h"

int g_registered_command1=0;
int g_registered_command2=0;

gaccel_register_t acreg1=
{
    {0,0,0},
    "Xenakios : Show/hide CDP front-end"
};

gaccel_register_t acreg2=
{
    {0,0,0},
    "Xenakios : Reset CDP front-end"
};

HINSTANCE g_hInst;
HWND g_reaper_mainwnd;

std::unique_ptr<AudioFormatManager> g_format_manager;
std::unique_ptr<AudioThumbnailCache> g_thumb_cache;
std::unique_ptr<PropertiesFile> g_propsfile;
File g_cdp_binaries_dir;
File g_stand_alone_render_dir;

bool g_is_running_as_plugin=false;

CDP_processor_info init_texture_simple()
{
    /*
    texture simple mode infile [infile2] outfile notedata outdur packing scatter tgrid
    sndfirst sndlast mingain maxgain mindur maxdur minpich maxpich
    [-aatten] [-pposition] [-sspread] [-rseed] [-w]
    */
    CDP_processor_info info("Texture Simple","texture","simple","5",false,true);
    parameter_info notedataparam("Base pitch",60.0,1.0,127.0);
    notedataparam.m_cmd_arg_formatter=[](parameter_info* parinfo)
    {
        String parname="basepitch";
        String filename=get_audio_render_path()+"/"+c_file_prefix+parname+String(Time::currentTimeMillis())+".txt";
        File txt_file(filename);
        FileOutputStream* os=txt_file.createOutputStream();
        if (os!=nullptr)
        {
            (*os) << String::formatted("%f\n",parinfo->m_current_value);
            delete os;
            return std::make_pair(filename,true);
        }
        return std::make_pair(String(),false);
    };
    info.m_parameters.push_back(notedataparam);
    parameter_info outdurpar{"Out duration",5.0,1.0,60.0};
    outdurpar.m_skewed=true;
    outdurpar.m_skew=0.50;
    info.m_parameters.push_back(outdurpar);
    info.m_parameters.push_back({"Packing",0.2,0.025,1.0,true});
    info.m_parameters.push_back({"Scatter",0.0,0.00,5.0,true});
    info.m_parameters.push_back({"Time Grid",0.0,0.0,500.0,true});
    info.m_parameters.push_back({"N/A",1.0,1.00,1.0});
    info.m_parameters.push_back({"N/A",1.0,1.00,1.0});
    info.m_parameters.push_back({"Min volume",64.0,1.00,127.0,true});
    info.m_parameters.push_back({"Max volume",64.0,1.00,127.0,true});
    info.m_parameters.push_back({"Min duration",0.1,0.02,5.0,true});
    info.m_parameters.push_back({"Max duration",0.5,0.02,5.0,true});
    info.m_parameters.push_back({"Min pitch",60,1.0,127.0,true});
    info.m_parameters.push_back({"Max pitch",60,1.0,127.0,true});
    for (auto& e : info.m_parameters)
    {
        if (e.m_can_automate==true)
        {
            e.m_envelope_time_scaling_func=[](CDP_processor_info* procinfo)
            {
                return procinfo->m_parameters[2].m_current_value;
            };
        }
    }
    return info;
}

void check_and_fix_environment()
{
#ifdef WIN32
    char buf[1024];
    auto result=GetEnvironmentVariableA("CDP_SOUND_EXT",buf,1024);
    if (result==0)
    {
        Logger::writeToLog("CDP sound file extension environment variable not set");
        SetEnvironmentVariableA("CDP_SOUND_EXT","wav");
        result=GetEnvironmentVariableA("CDP_SOUND_EXT",buf,1024);
        if (result!=0)
        {
            Logger::writeToLog("Set CDP_SOUND_EXT for the process!");
        }
    }
    result=GetEnvironmentVariableA("CDP_NOCLIP_FLOATS",buf,1024);
    if (result==0)
    {
        Logger::writeToLog("CDP_NOCLIP_FLOATS environment variable not set");
        SetEnvironmentVariableA("CDP_NOCLIP_FLOATS","1");
        result=GetEnvironmentVariableA("CDP_NOCLIP_FLOATS",buf,1024);
        if (result!=0)
        {
            Logger::writeToLog("Set CDP_NOCLIP_FLOATS for the process!");
        }
    }
#else
    if (putenv(strdup("CDP_SOUND_EXT=wav"))!=0)
        Logger::writeToLog("Could not set CDP_SOUND_EXT environment variable");
    if (putenv(strdup("CDP_NOCLIP_FLOATS=1"))!=0)
        Logger::writeToLog("Could not set CDP_NOCLIP_FLOATS environment variable");
#endif
}



File get_cdp_binaries_location(PropertiesFile* propfile)
{
    File cdpdir=File(propfile->getValue("general/cdp_bin_loc"));
    if (cdpdir.exists()==false)
    {
        FileChooser chooser("Choose CDP binaries location");
        bool result=chooser.browseForDirectory();
        if (result==true)
        {
            File temp=chooser.getResult();

#ifdef WIN32
            auto test_files=make_string_array({"pvoc.exe","modify.exe","sfedit.exe"});
#else
            auto test_files=make_string_array({"pvoc","modify","sfedit"});
#endif

            int cnt=0;
            for (int i=0;i<test_files.size();++i)
                if (does_file_exist(temp.getFullPathName()+"/"+test_files[i]))
                    ++cnt;
            if (cnt!=test_files.size())
            {
                AlertWindow::showNativeDialogBox("Warning","The location does not appear to have the CDP binaries",false);
            } else
            {
                propfile->setValue("general/cdp_bin_loc",temp.getFullPathName());
                return temp;
            }

        }
    }
    return cdpdir;
}

WNDPROC g_old_window_proc;

//#ifdef WIN32
LRESULT CALLBACK my_window_proc(
  HWND hwnd,
  UINT uMsg,
  WPARAM wParam,
  LPARAM lParam
);
//#endif

class CDP_holder : public Timer
{
public:
    CDP_holder()
    {
    }
#ifdef CDP_VST_ENABLED
	void initPluginHosting()
	{
		File pardir = g_propsfile->getFile().getParentDirectory();
		File plugscanfile(pardir.getFullPathName() + "/plugininfos.xml");
		if (plugscanfile.existsAsFile() == true)
		{
			juce::XmlDocument doc(plugscanfile);
			XmlElement* elem = doc.getDocumentElement();
			m_pluginlist.recreateFromXml(*elem);
			delete elem;
			if (m_pluginlist.getNumTypes() > 0)
			{
				Logger::writeToLog("previous plugin scan results loaded");
			}
		}
		VSTPluginFormat vstformat;
		
		PluginDirectoryScanner plugdirscan(m_pluginlist, vstformat,
			FileSearchPath("C:/Program Files/VST_Plugins_x64;C:/Program Files/VSTPlugins"), 
			true, File::nonexistent);
		String foo;
		while (plugdirscan.scanNextFile(true, foo) == true)
		{
			//Logger::writeToLog(foo);
		}
		int count = 0;
		for (auto& plug : m_pluginlist)
		{
			CDP_processor_info info("VST/"+plug->name, "vstplugin", "", "", false, false);
			info.m_pluginname = plug->name;
			info.m_plugin_id = count;
			m_proc_infos.push_back(info);
			++count;
		}
		
		XmlElement* xml = m_pluginlist.createXml();
		xml->writeToFile(plugscanfile,"");
		delete xml;
	}
#endif
    void initialise(const String&)
    {
        g_format_manager=jcdp::make_unique<AudioFormatManager>();
        g_format_manager->registerBasicFormats();
        g_thumb_cache=jcdp::make_unique<AudioThumbnailCache>(32);
        if (g_is_running_as_plugin==false)
            m_audio_delegate=jcdp::make_unique<juce_audio_preview>(g_format_manager.get());
        else
            m_audio_delegate=jcdp::make_unique<reaper_audio_preview>();
        PropertiesFile::Options poptions;
        poptions.applicationName="JuceCDP";
        poptions.folderName="JuceCDP";
        poptions.commonToAllUsers=false;
        poptions.doNotSave=false;
        poptions.storageFormat=PropertiesFile::storeAsXML;
        poptions.millisecondsBeforeSaving=1000;
        poptions.ignoreCaseOfKeyNames=false;
        poptions.processLock=nullptr;
        poptions.filenameSuffix=".xml";
        poptions.osxLibrarySubFolder="Application Support";

        g_propsfile=jcdp::make_unique<PropertiesFile>(poptions);

        g_cdp_binaries_dir=get_cdp_binaries_location(g_propsfile.get());
        if (g_is_running_as_plugin==false)
            g_stand_alone_render_dir=File(g_propsfile->getValue("render_dir"));
        
#ifdef CDP_VST_ENABLED
		initPluginHosting();
#endif
		CDP_processor_info info=CDP_processor_info("Modify Brassage Brassage","modify","brassage","6",false,true);
        info.m_parameters.push_back({"Velocity",0.5,0.01,2.0,true,"",true,0.75,0.01});
        info.m_parameters.push_back({"Density",2.0,0.1,10.0,true});
        info.m_parameters.push_back({"Grain size",50.0,2.0,500.0,true});
        info.m_parameters.push_back({"Pitch shift",-6.0,-24.0,24.0,true});
        info.m_parameters.push_back({"Amplitude",0.75,0.01,1.0,true});
        info.m_parameters.push_back({"Space",0.5,0.0,1.0,true});
        info.m_parameters.push_back({"Fade in",5.0,1.00,20.0,true});
        info.m_parameters.push_back({"Fade out",5.0,1.00,20.0,true});
        m_proc_infos.push_back(info);
        
		// modify brassage 7 infile outfile velocity density hvelocity hdensity
//grainsize   pitchshift   amp  space   bsplice   esplice
//hgrainsize hpitchshift hamp hspace hbsplice hesplice
		
		info = CDP_processor_info("Modify Brassage Full Monty", "modify", "brassage", "7", false, true);
		info.m_parameters.push_back({ "Velocity low",0.5,0.01,2.0,true,"",true,0.75,0.01 });
		info.m_parameters.push_back({ "Density low",2.0,0.1,10.0,true });
		info.m_parameters.push_back({ "Velocity high",0.55,0.01,2.0,true,"",true,0.75,0.01 });
		info.m_parameters.push_back({ "Density high",2.0,0.1,10.0,true });
		info.m_parameters.push_back({ "Grain size low",50.0,2.0,500.0,true });
		info.m_parameters.push_back({ "Pitch shift low",-6.0,-24.0,24.0,true });
		info.m_parameters.push_back({ "Amplitude low",0.75,0.01,1.0,true });
		info.m_parameters.push_back({ "Space low",0.1,0.0,1.0,true });
		info.m_parameters.push_back({ "Fade in low",5.0,1.00,20.0,true });
		info.m_parameters.push_back({ "Fade out low",5.0,1.00,20.0,true });
		info.m_parameters.push_back({ "Grain size high",5.0,1.00,20.0,true });
		info.m_parameters.push_back({ "Pitch shift high",-2.0,-24.0,24.0,true });
		info.m_parameters.push_back({ "Amplitude high",0.75,0.01,1.0,true });
		info.m_parameters.push_back({ "Space high",0.9,0.0,1.0,true });
		info.m_parameters.push_back({ "Fade in high",5.0,1.00,20.0,true });
		info.m_parameters.push_back({ "Fade out high",5.0,1.00,20.0,true });
		m_proc_infos.push_back(info);


        info=CDP_processor_info("Modify Radical Shred","modify","radical","2",false,false);
        info.m_parameters.push_back({"Iterations",1.0,1.0,32.0});
        info.m_parameters.push_back({"Chunk length",0.1,0.05,2.0});
        m_proc_infos.push_back(info);

        info=CDP_processor_info("Modify Speed","modify","speed","2",false,true);
        info.m_parameters.push_back({"Semitones",0.0,-24.0,24.0,true});
        m_proc_infos.push_back(info);

        info=CDP_processor_info("Modify Stack","modify","stack","",false,true);
        info.m_parameters.push_back({"Transpose",-12.0,-12.0,12.0});
        info.m_parameters.push_back({"Num layers",2.0,2.0,8.0});
        info.m_parameters.push_back({"Lean",1.0,0.1,2.0});
        parameter_info attackparinfo("Attack offset",0.0,0.0,1.0);
        attackparinfo.m_notifs=parameter_info::waveformmarker;
        info.m_parameters.push_back(attackparinfo);
        info.m_parameters.push_back({"Gain",1.0,0.1,2.0});
        info.m_parameters.push_back({"Duration",1.0,0.01,1.0});
        m_proc_infos.push_back(info);

        info=CDP_processor_info("Distort Repeat","distort","repeat","",false,true,true);
        info.m_parameters.push_back({"Multiplier",2.0,2.0,16.0,true});
        info.m_parameters.push_back({"Cycle cnt",1.0,1.0,8.0,true,"-c"});
        m_proc_infos.push_back(info);

        // grain timewarp infile outfile timestretch_ratio [-blen] [-lgate] [-hminhole] [-twinsize] [-x]

        info=CDP_processor_info("Grain Timewarp","grain","timewarp","",false,true,true);
        info.m_parameters.push_back({"Ratio",0.5,0.1,2.0,true});
        m_proc_infos.push_back(info);

        info=CDP_processor_info("Distort Pitch","distort","pitch","",false,true,true);
        info.m_parameters.push_back({"Pitch amount",0.2,0.01,8.0,true});
        info.m_parameters.push_back({"Cycle cnt",32.0,2.0,128.0,true,"-c"});
        m_proc_infos.push_back(info);

        info=CDP_processor_info("Distort Interpolate","distort","interpolate","",false,true,true);
        info.m_parameters.push_back({"Multiplier",4.0,2.00,64.0,true});
        m_proc_infos.push_back(info);

        info=CDP_processor_info("Envel Warp Exaggerate","envel","warp","3",false,false);
        info.m_parameters.push_back({"Window size",20.0,5.0,100.0});
        info.m_parameters.push_back({"Amount",0.75,0.05,4.0,true,"",true,0.5,0.01});
        m_proc_infos.push_back(info);

        info=CDP_processor_info("Blur Blur","blur","blur","",true,false);
        info.m_parameters.push_back({"Blur amount",50.0,1.0,1000.0,true,"",true,0.3});
        m_proc_infos.push_back(info);

        info=CDP_processor_info("Blur Noise","blur","noise","",true,false);
        info.m_parameters.push_back({"Noise amount",0.2,0.0,1.0,true});
        m_proc_infos.push_back(info);

        // blur suppress infile outfile N

        info=CDP_processor_info("Blur Suppress","blur","suppress","",true,false);
        info.m_parameters.push_back({"Amount",4.0,1.0,16.0,true});
        m_proc_infos.push_back(info);

        info=CDP_processor_info("Stretch Time","stretch","time","1",true,true);
        info.m_parameters.push_back({"Time factor",2.0,1.0,64.0,true,"",true,0.25,0.01});
        m_proc_infos.push_back(info);

        info=CDP_processor_info("Stretch Spectrum","stretch","spectrum","1",true,false);
        info.m_parameters.push_back({"Freq divide",64.0,64,10000.0,false,"",true,0.25,10.0});
        info.m_parameters.push_back({"Max stretch",2.0,0.1,2.0,false,"",false,1.0,0.01});
        info.m_parameters.push_back({"Exponent",0.5,0.1,2.0,false,"",false,1.0,0.01});
        info.m_parameters.push_back({"Depth",1.0,0.0,1.0,true,"-d",false,1.0,0.01});
        m_proc_infos.push_back(info);

        info=CDP_processor_info("Focus Step","focus","step","",true,false);
        info.m_parameters.push_back({"Step duration",0.5,0.1,1.0});
        m_proc_infos.push_back(info);

        info=CDP_processor_info("Repitch Transpose","repitch","transpose","3",true,false);
        info.m_parameters.push_back({"Semitones",0.0,-24.0,24.0,true});
        m_proc_infos.push_back(info);

        info=CDP_processor_info("Focus Accu","focus","accu","",true,false);
        info.m_parameters.push_back({"Decay",0.5,0.01,1.0,false,"-d"});
        info.m_parameters.push_back({"Gliss",-0.05,-11.7,11.7,false,"-g"});
        m_proc_infos.push_back(info);

        info=CDP_processor_info("Strange Waver","strange","waver","2",true,false);
        info.m_parameters.push_back({"Rate",1.0,0.1,32.0});
        info.m_parameters.push_back({"Stretch",100.0,-1.0,2205.0});
        info.m_parameters.push_back({"Bottom freq",100.0,20.0,4096.0});
        info.m_parameters.push_back({"Shape",1.0,0.1,2.0});
        m_proc_infos.push_back(info);

        m_proc_infos.push_back(init_texture_simple());
        /*
        // Seems to be a processor that is very finicky about the parameter values,
        // probably not worth adding here as the logic for the correct parameter values
        // would need to be reimplemented. Or perhaps extend scramble 2 is just very buggy...
        // extend scramble 2 infile outfile seglen scatter outdur [-wsplen] [-sseed] [-b] [-e]
        info=CDP_processor_info("Extend Scramble","extend","scramble","2",false,true);
        info.m_parameters.push_back({"Segment length",0.5,0.01,2.0});
        info.m_parameters.push_back({"Scatter",0.1,0.0,2.0});
        info.m_parameters.push_back({"Output length",5.0,0.5,15.0});
        m_proc_infos.push_back(info);
        */

        /*
        // gate.exe seems to crash right away when launched, so I guess there isn't much to do in the front end
        // about that.
        // gate gate mode infile outfile gatelevel
        info=CDP_processor_info("Gate 2","gate","gate","2",false,true);
        info.m_parameters.push_back({"Threshold",-20.0,-96.0,0.0});
        m_proc_infos.push_back(info);
        */

        //envnu peakchop 1 insndfile outsndfile wsize pkwidth risetime tempo gain

        info=CDP_processor_info("Envnu Peakchop","envnu","peakchop","1",false,true);
        info.m_parameters.push_back({"Window size",50.0,1.0,64.0});
        info.m_parameters.push_back({"Peak Width",20.0,0.0,1000.0});
        info.m_parameters.push_back({"Rise Time",10.0,0.0,100.0});
        info.m_parameters.push_back({"Tempo",90.0,20.0,3000.0,true,"",true,0.3});
        info.m_parameters.push_back({"Gain",1.0,0.0,1.0,true});


        m_proc_infos.push_back(info);

        std::sort(m_proc_infos.begin(),m_proc_infos.end(),
                  [](const CDP_processor_info& lhs, const CDP_processor_info &rhs)
        {
            return lhs.m_title<rhs.m_title;
        });

        m_dlg=jcdp::make_unique<cdp_main_dialog>(m_audio_delegate.get(),&m_proc_infos,&m_pluginlist);
        if (g_is_running_as_plugin==true)
        {
#ifdef WIN32
            m_dlg->addToDesktop(m_dlg->getDesktopWindowStyleFlags(),g_reaper_mainwnd);
#else
            m_dlg->addToDesktop(m_dlg->getDesktopWindowStyleFlags(),0);
            //m_dlg->setAlwaysOnTop(true);

            makeWindowFloatingPanel(m_dlg.get());
#endif

#ifdef DOCKED_WINDOW
            HWND hwnd=(HWND)m_dlg->getPeer()->getNativeHandle();
            DockWindowAddEx(hwnd,"CDP Front-end","cdpfe",true);
            DockWindowActivate(hwnd);
            g_old_window_proc=(WNDPROC)SetWindowLongPtr(hwnd,GWL_WNDPROC,(LONG_PTR)my_window_proc);
#endif
            startTimer(500);
        }
        m_dlg->setBounds(10,60,800,600);
        String rstr=g_propsfile->getValue("windowrect");
        if (rstr.isEmpty()==false)
        {
            m_dlg->setBounds(juce::Rectangle<int>::fromString(rstr));
        }
		if (m_dlg->getY() < 60)
			m_dlg->setTopLeftPosition(m_dlg->getX(), 60);
		if (g_is_running_as_plugin == false)
		{
			m_dlg->setVisible(true);
			m_dlg->toFront(true);
		}
    }
    void shutdown()
    {
        //if (g_is_running_as_plugin==false)
            g_propsfile->setValue("windowrect",m_dlg->getBounds().toString());
        g_format_manager.reset();
        g_propsfile.reset();
    }

    void timerCallback()
    {
#if defined(WIN32) && defined(DOCKED_WINDOW)
		HWND hwnd=(HWND)m_dlg->getPeer()->getNativeHandle();
        HWND ph=GetParent(hwnd);
        RECT pr;
        if (GetWindowRect(ph,&pr)!=FALSE)
        {
            m_dlg->setSize(pr.right-pr.left,pr.bottom-pr.top-18);
        }
#endif
        poll_reaper_items();
    }

    void poll_reaper_items()
    {
#ifdef BUILD_CDP_FRONTEND_PLUGIN
		if (m_dlg->isVisible()==true)
        {
            int num_items=CountSelectedMediaItems(0);
            for (int i=0;i<num_items;++i)
            {
                MediaItem* item=GetSelectedMediaItem(0,i);
                MediaItem_Take* take=GetActiveTake(item);
                PCM_source* source=(PCM_source*)GetSetMediaItemTakeInfo(take,"P_SOURCE",nullptr);
                if (source)
                {
                    String source_fn(source->GetFileName());
                    if (has_supported_media_type(source)==true)
                    {
                        m_dlg->setEnabled(true);
                        m_dlg->update_status_label();
                        double source_len=source->GetLength();
                        double take_startoffset=*(double*)GetSetMediaItemTakeInfo(take,"D_STARTOFFS",nullptr);
                        double item_length=*(double*)GetSetMediaItemInfo(item,"D_LENGTH",nullptr);
                        double take_end_offset=take_startoffset+item_length;
                        time_range trange(0.0,item_length);
                        if (fuzzy_is_zero(take_startoffset) && fuzzy_compare(item_length,source_len))
                            trange=time_range();

                        if (source_fn!=m_dlg->m_in_fn
                                || (m_dlg->m_custom_time_set==false && m_dlg->get_input_time_range()!=trange))

                        {
                            if (m_dlg->m_follow_item_selection==true)
                            {
                                m_dlg->set_reaper_take(take);
                            }
                        }
                    } else
                    {
                        m_dlg->setEnabled(false);
                        m_dlg->m_status_label->setText("UNSUPPORTED MEDIA TYPE",dontSendNotification);
                        //m_dlg->setName(source_fn+" (unsupported media type)");
                    }
                }
            }
        }
#endif
    }
    void set_window_visible(bool b)
    {
#ifndef WIN32
        if (b==true)
        {
            m_dlg->addToDesktop(m_dlg->getDesktopWindowStyleFlags(),GetMainHwnd());
            m_dlg->setAlwaysOnTop(true);
        }
#endif
        m_dlg->setVisible(b);
        g_propsfile->setValue("windowvisible",b);
        return;
        HWND hwnd=(HWND)m_dlg->getPeer()->getNativeHandle();
        if (b==true)
        {
            m_dlg->addToDesktop(m_dlg->getDesktopWindowStyleFlags(),g_reaper_mainwnd);
            //if (g_old_window_proc==nullptr)
            //    g_old_window_proc=(WNDPROC)SetWindowLongPtr(hwnd,GWLP_WNDPROC,(LONG_PTR)my_window_proc);
            DockWindowAddEx(hwnd,"CDP Front-end","cdpfe",true);
            DockWindowActivate(hwnd);
        } else
        {
            DockWindowRemove(hwnd);
        }
    }
    bool is_window_visible() const { return m_dlg->isVisible(); }
    void toggle_window_visible()
    {
        set_window_visible(!is_window_visible());
        return;
    }

    std::unique_ptr<IJCDPreviewPlayback> m_audio_delegate;
    std::vector<CDP_processor_info> m_proc_infos;
	KnownPluginList m_pluginlist;
    std::unique_ptr<cdp_main_dialog> m_dlg;
};

std::unique_ptr<CDP_holder> g_holder;

//#ifdef WIN32
LRESULT CALLBACK my_window_proc(
  HWND hwnd,
  UINT uMsg,
  WPARAM wParam,
  LPARAM lParam
)
{
    readbg() << "close tab?\n";
    //Logger::writeToLog(String::formatted("%d %d %d %d",hwnd,uMsg,wParam,lParam));
    if (uMsg==WM_COMMAND && (wParam==IDOK || wParam==IDCANCEL))
    {
        //Logger::writeToLog("close tab requested?");
        if (g_holder!=nullptr)
        {
            g_holder->toggle_window_visible();
            return 0;
        }
    }
    if (uMsg==WM_DESTROY)
    {
        //Logger::writeToLog("WM_DESTROY");
        //return 1;
    }
    return g_old_window_proc(hwnd,uMsg,wParam,lParam);
}
//#endif

bool g_juce_inited=false;

bool hookCommandProc(int command, int)
{
    if (g_registered_command1!=0 && command == g_registered_command1)
    {
        if (g_juce_inited==false)
        {
            check_and_fix_environment();
            initialiseJuce_GUI();
            g_juce_inited=true;
        }
        if (g_holder==nullptr)
        {
            g_holder=jcdp::make_unique<CDP_holder>();
            g_holder->initialise(String());
        }
        g_holder->toggle_window_visible();
        return true;
    }
    if (g_registered_command2!=0 && command == g_registered_command2)
    {
        if (g_juce_inited==false || g_holder==nullptr)
            return true;
        bool was_visible=g_holder->is_window_visible();
        g_holder=jcdp::make_unique<CDP_holder>();
        g_holder->initialise(String());
        g_holder->set_window_visible(was_visible);
        return true;
    }
    return false;
}

bool on_value_action(KbdSectionInfo *sec, int command, int val, int valhw, int relmode, HWND hwnd)
{
    if (sec!=nullptr && sec->uniqueID==0)
    {
        if (g_registered_command1!=0 && command == g_registered_command1)
        {
            if (g_juce_inited==false)
            {
                check_and_fix_environment();
                initialiseJuce_GUI();
                g_juce_inited=true;
            }
            if (g_holder==nullptr)
            {
                g_holder=jcdp::make_unique<CDP_holder>();
                g_holder->initialise(String());
            }
            g_holder->toggle_window_visible();
            return true;
        }
        if (g_registered_command2!=0 && command == g_registered_command2)
        {
            if (g_juce_inited==false || g_holder==nullptr)
                return true;
            bool was_visible=g_holder->is_window_visible();
            g_holder=jcdp::make_unique<CDP_holder>();
            g_holder->initialise(String());
            g_holder->set_window_visible(was_visible);
            return true;
        }
    }

    //Logger::writeToLog(String::formatted("action with value : %d %d %d",command,val,valhw));
    return false;
}

class MyJUCEApp  : public JUCEApplication
{
public:
    MyJUCEApp()  {}
    ~MyJUCEApp() {}
    void initialise (const String& commandLine)
    {
        m_holder.initialise(commandLine);
    }

    void shutdown()
    {
        m_holder.shutdown();
    }



    const String getApplicationName()
    {
        return "CDP Frontend";
    }
    const String getApplicationVersion()
    {
        return "1.0";
    }


private:
    CDP_holder m_holder;

};

static juce::JUCEApplicationBase* juce_CreateApplication() { return new MyJUCEApp(); }

#ifdef BUILD_CDP_FRONTEND_PLUGIN

accelerator_register_t* g_kbdhook=nullptr;

int my_accel_translate(MSG *msg, accelerator_register_t *ctx)
{
    if (g_holder==nullptr)
        return 0;
    if (g_holder->m_dlg->getPeer()==nullptr)
        return 0;
    HWND h_maindialog=(HWND)g_holder->m_dlg->getPeer()->getNativeHandle();
    HWND h_modalcomponent=0;
    if (Component::getCurrentlyFocusedComponent()!=nullptr)
    {
        h_modalcomponent=(HWND)Component::getCurrentlyFocusedComponent()->getPeer()->getNativeHandle();
    }
    if (h_maindialog==msg->hwnd || h_modalcomponent==msg->hwnd)
    {
        if (msg->message==WM_KEYDOWN || msg->message==WM_KEYUP || msg->message==WM_CHAR)
        {
            return -1;
        }
    }
    return 0;
}

#define IMPAPI(x) if (!((*((void **)&(x)) = (void *)rec->GetFunc(#x)))) errcnt++;

extern "C"
{

REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE hInstance, reaper_plugin_info_t *rec)
{
    g_hInst=hInstance;
    if (rec)
    {
        juce::JUCEApplicationBase::createInstance=nullptr;
		if (REAPERAPI_LoadAPI(rec->GetFunc) > 0)
		{
			return 0;
		}
		g_reaper_mainwnd=rec->hwnd_main;
		g_is_running_as_plugin=true;

        acreg1.accel.cmd = g_registered_command1 = rec->Register("command_id", (void*)"XEN_TOGGLESHOWCDPFRONTEND");
        rec->Register("gaccel",&acreg1);

        acreg2.accel.cmd = g_registered_command2 = rec->Register("command_id", (void*)"XEN_RESETCDPFRONTEND");
        rec->Register("gaccel",&acreg2);

        //rec->Register("hookcommand", (void*)hookCommandProc);
        rec->Register("hookcommand2", (void*)on_value_action);

        g_kbdhook=new accelerator_register_t;
        g_kbdhook->isLocal=true;
        g_kbdhook->translateAccel=my_accel_translate;
        g_kbdhook->user=nullptr;
        rec->Register("accelerator",(void*)g_kbdhook);
        return 1;
    }
    if (g_juce_inited==true && g_holder!=nullptr)
    {
        g_holder->shutdown();
        g_holder.reset();
        g_thumb_cache.reset();
        shutdownJuce_GUI();
        delete g_kbdhook;
    }
    return 0;
}
}
#else

#ifdef WIN32
int __stdcall WinMain (HINSTANCE, HINSTANCE, const LPSTR, int)
#else
int main(int argc, char** argv)
#endif
{
	check_and_fix_environment();
    juce::JUCEApplicationBase::createInstance = &juce_CreateApplication;
    int rc=juce::JUCEApplicationBase::main();
    g_thumb_cache.reset();
    return rc;
}
#endif

