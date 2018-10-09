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

#include "jcdp_wavecomponent.h"
#include <memory>
#include "jcdp_utilities.h"
#include "jcdp_processor.h"

#ifdef WIN32
#undef max
#endif

extern std::unique_ptr<AudioThumbnailCache> g_thumb_cache;
extern std::unique_ptr<AudioFormatManager> g_format_manager;

WaveFormComponent::WaveFormComponent(bool usetimer) :
    m_waveformcolour(Colours::darkcyan)
{
    if (usetimer==true)
    {
        startTimer(50);
    }
    FilePositionFunc=[](){return 0.0;};
    setWantsKeyboardFocus(true);
    m_bubble=jcdp::make_unique<BubbleMessageComponent>();
    addChildComponent(m_bubble.get());
}

WaveFormComponent::~WaveFormComponent()
{
    delete m_thumb;
}

void WaveFormComponent::paint(Graphics &g)
{
    g.fillAll(Colours::black);
    g.setColour(m_waveformcolour);
    juce::Rectangle<int> rect(0,0,getWidth(),getHeight());
	bool file_is_set = false;
	if (m_thumb!=nullptr)
    {
        double soundlen=m_thumb->getTotalLength();
		if (soundlen > 0.0)
		{
			file_is_set = true;
			m_thumb->drawChannels(g, rect, soundlen*m_view_start, soundlen*m_view_end, 1.0);
			g.setColour(Colours::white);
			String text;
			if (m_render_elapsed_time > 0.0)
			{
				double factor = soundlen / m_render_elapsed_time;
				text = m_audio_fn + String::formatted(" %.2f secs (%.1fx realtime)", soundlen, factor);
			}
			else
				text = m_audio_fn + String::formatted(" %.2f secs", soundlen);
			g.drawText(text, 5, 5, 700, 20, Justification::centredLeft, false);
			if (get_active_time_range().isValid() == true)
			{
				//double xcor1=(double)getWidth()/soundlen*get_active_time_range().start();
				//double widpixels=(double)getWidth()/soundlen*get_active_time_range().length();
				double xcor1 = scale_value_from_range_to_range(get_active_time_range().start(),
					m_view_start*soundlen,
					m_view_end*soundlen,
					0.0,
					(double)getWidth());
				double xcor2 = scale_value_from_range_to_range(get_active_time_range().end(),
					m_view_start*soundlen,
					m_view_end*soundlen,
					0.0,
					(double)getWidth());
				double widpixels = xcor2 - xcor1;
				g.setColour(Colour(Colours::white).withAlpha(0.5f));
				g.fillRect(xcor1, 0.0, widpixels, getHeight());
				g.setColour(Colours::white);
			}
			if (isTimerRunning() == true)
			{
				double xcor = getWidth() / soundlen*FilePositionFunc();
				g.drawLine(xcor, 0.0, xcor, getHeight());
			}
			if (m_has_handle == true)
			{
				if (m_hot_area == ha_attackmarker)
					g.setColour(Colours::tomato);
				else g.setColour(Colours::white);
				double xcor = scale_value_from_range_to_range(m_handle_pos,
					m_view_start,
					m_view_end,
					0.0,
					(double)getWidth());
				g.drawLine(xcor, 0.0, xcor, getHeight());
			}
			if (m_env_editor != nullptr)
			{
				Colour envcolor(Colours::yellow);
				bool draw_handles = true;
				if (m_edit_mode == em_waveform)
				{
					envcolor = envcolor.withAlpha(0.5f);
					draw_handles = false;
				}
				m_env_editor->m_envelope_colour = envcolor;
				m_env_editor->m_draw_handles = draw_handles;
				m_env_editor->paint(g, getBounds());
			}
		}
    } 
	if (file_is_set==false)
        g.drawText("No sound file set",5,5,150,20,Justification::centredLeft,true);
    if (hasKeyboardFocus(false)==true)
    {
        g.setColour(Colour((uint8_t)255,255,255,(uint8_t)128));
        g.drawRect(0,0,getWidth(),getHeight(),2);
    }
}

void WaveFormComponent::set_file(String fn)
{
    m_audio_fn=fn;
    File thumbfile(fn);
    m_thumb_source=new FileInputSource(thumbfile);
    delete m_thumb;
    m_thumb=new AudioThumbnail(64,*g_format_manager,*g_thumb_cache);
    m_thumb->setSource(m_thumb_source);
    m_thumb->addChangeListener(this);
    repaint();
}

void WaveFormComponent::mouseDown(const MouseEvent &event)
{
    if (m_thumb==nullptr)
        return;
    if (m_edit_mode==em_envelope)
    {
		if (m_hot_area == ha_none && event.mods.isRightButtonDown() == true)
		{
			PopupMenu pop;
			for (int i = 0; i < m_parameter_names.size(); ++i)
				pop.addItem(10+i, m_parameter_names[i]);
			const int result = pop.show();
			if (result >= 10)
			{
				if (EnvelopeChangeRequestedFunc)
					EnvelopeChangeRequestedFunc(m_parameter_names[result - 10]);
			}
			return;
		}
		if (m_hot_area==ha_none && event.mods.isCtrlDown()==false)
        {
            if (m_env_editor!=nullptr)
            {
                m_env_editor->on_mouse_event(ISubComponent::met_press,event);
                if (m_env_editor->is_hot()==true)
                    return;
            }
        }
    }

    m_hot_area=get_hot_area(event);
    double soundlen=m_thumb->getTotalLength();
    double selx0=scale_value_from_range_to_range((double)event.x,
                                                 0.0,
                                                 (double)getWidth(),
                                                 m_view_start*soundlen,
                                                 m_view_end*soundlen);
    if (m_hot_area==ha_none)
    {
        m_time_sel_drag_start=selx0;
    }
    if (m_hot_area==ha_none && isTimerRunning()==true && m_thumb!=nullptr)
    {
        double seekpos=soundlen/getWidth()*event.x;
        if (OnSeekFunc)
            OnSeekFunc(seekpos);
    }
    m_old_time_range=get_active_time_range();

    repaint();
}

void WaveFormComponent::mouseUp(const MouseEvent &event)
{
    if (m_thumb==nullptr)
        return;
    m_hot_area=ha_none;
    if (m_edit_mode!=em_envelope && m_which_dirty!=ha_none)
    {
        //Logger::writeToLog(String::formatted("Time selection at mouse up %f %f",m_time_range.start(),m_time_range.end()));
        sendChangeMessage();
    }
    m_which_dirty=ha_none;
    m_old_time_range=get_active_time_range();
    if (m_env_editor!=nullptr)
    {
        m_env_editor->on_mouse_event(ISubComponent::met_release,event);
        if (m_env_editor->is_state_dirty()==true)
        {
            //Logger::writeToLog("envelope dirty on mouse up");
            sendChangeMessage();
        }
    }
    repaint();
}

void WaveFormComponent::mouseMove(const MouseEvent &event)
{
    if (m_thumb==nullptr)
        return;
    if (m_edit_mode==em_envelope)
    {
        if (m_env_editor!=nullptr)
        {
            m_hot_area=ha_none;
            m_env_editor->on_mouse_event(ISubComponent::met_move,event);
            if (m_env_editor->is_hot()==true)
                return;
        }
    }

    m_hot_area=get_hot_area(event);
    if (m_hot_area==ha_timesel_left || m_hot_area==ha_timesel_right)
        setMouseCursor(MouseCursor::LeftRightResizeCursor);
    if (m_hot_area==ha_attackmarker && m_has_handle==true)
        setMouseCursor(MouseCursor::LeftEdgeResizeCursor);
    if (m_hot_area==ha_timesel_all)
        setMouseCursor(MouseCursor::DraggingHandCursor);
    if (m_hot_area==ha_none)
    {
        setMouseCursor(MouseCursor::NormalCursor);

    }

}

void WaveFormComponent::mouseDrag(const MouseEvent &event)
{
    if (isTimerRunning()==true)
        return;
    if (m_thumb==nullptr)
        return;
    if (m_edit_mode==em_envelope && m_hot_area==ha_none && m_env_editor!=nullptr)
    {
        m_env_editor->on_mouse_event(ISubComponent::met_drag,event);
        if (m_env_editor->is_hot()==true)
        {
            //setTooltip(m_env_editor->get_tool_tip());
            repaint();
            return;
        }
    }
    double soundlen=m_thumb->getTotalLength();
    double selx0=scale_value_from_range_to_range((double)event.x,
                                                 0.0,
                                                 (double)getWidth(),
                                                 m_view_start*soundlen,
                                                 m_view_end*soundlen);
    if (m_hot_area==ha_none)
    {
        if (m_edit_mode==em_envelope && event.mods.isCtrlDown()==false)
            return;
        double new_edge=bound_value(0.0,selx0,soundlen);
        double new_left_edge=m_time_sel_drag_start;
        if (new_edge<new_left_edge)
            std::swap(new_edge,new_left_edge);
        if (new_edge-new_left_edge<0.04)
            return;
        get_active_time_range()=time_range(new_left_edge,new_edge);
        m_which_dirty=ha_timesel_all;
    }
    if (m_has_handle==true && m_hot_area==ha_attackmarker)
    {
        double handlepos=scale_value_from_range_to_range((double)event.x,
                                                     0.0,
                                                     (double)getWidth(),
                                                     m_view_start,
                                                     m_view_end);
        m_handle_pos=bound_value(0.0,handlepos,1.0);
        m_which_dirty=ha_attackmarker;
    }

    if (m_hot_area==ha_timesel_left)
    {
        double new_left=bound_value(0.0,selx0,get_active_time_range().end()-0.04);
        get_active_time_range()=time_range(new_left,get_active_time_range().end());
        m_which_dirty=ha_timesel_left;
    }
    if (m_hot_area==ha_timesel_right)
    {
        double new_right=bound_value(get_active_time_range().start()+0.04,selx0,soundlen);
        get_active_time_range()=time_range(get_active_time_range().start(),new_right);
        m_which_dirty=ha_timesel_right;
    }
    if (m_hot_area==ha_timesel_all)
    {
        double time_delta0=(soundlen/getWidth())*event.getDistanceFromDragStartX();
        time_delta0*=1.0*(m_view_end-m_view_start);
        double len=m_old_time_range.length();
        double newt0=bound_value(0.0,m_old_time_range.start()+time_delta0,soundlen-len);
        double newt1=bound_value(len,m_old_time_range.end()+time_delta0,soundlen);
        get_active_time_range()=time_range(newt0,newt1);
        m_which_dirty=ha_timesel_all;
    }
    if (m_which_dirty!=ha_none)
        repaint();
}

bool WaveFormComponent::keyPressed(const KeyPress &key)
{
    if (m_thumb==nullptr)
        return false;
    if (m_edit_mode==em_envelope && m_env!=nullptr && key==KeyPress::deleteKey)
    {
        double soundlen=m_thumb->getTotalLength();
        double norm_env_start=1.0/soundlen*m_envelope_time_range.start();
        double norm_env_end=1.0/soundlen*m_envelope_time_range.end();
        m_env->m_env.delete_nodes_in_time_range(norm_env_start,norm_env_end);
        repaint();
        sendChangeMessage();
        return true;
    }
    if (m_edit_mode==em_waveform && key==KeyPress::deleteKey && CutFileFunc)
    {
        CutFileFunc(m_waveform_time_range);
        return true;
    }
    return false;
}

void WaveFormComponent::focusLost(Component::FocusChangeType)
{
    repaint();
}

void WaveFormComponent::set_parameter(parameter_info *env)
{
    if (m_env_editor==nullptr)
    {
        m_env_editor=jcdp::make_unique<envelope_editor>(this);
        m_env_editor->m_bubble=m_bubble.get();
        m_env_editor->EnvelopeLength=[this]()
        {
			if (m_thumb != nullptr)
				return m_thumb->getTotalLength();
			return 0.0;
		};
    }
    m_env=env;
    m_env_editor->set_envelope(env);
    repaint();
}

void WaveFormComponent::set_parameter_names(StringArray names)
{
	m_parameter_names = names;
}

void WaveFormComponent::set_view_range(double t0, double t1)
{
    m_view_start=t0;
    m_view_end=t1;
    if (m_env_editor!=nullptr)
        m_env_editor->m_view_range=std::make_pair(t0,t1);
    repaint();
}

WaveFormComponent::hot_area WaveFormComponent::get_hot_area(const MouseEvent &event)
{
    //double markerx=m_handle_pos*getWidth();
    double markerx=scale_value_from_range_to_range((double)m_handle_pos,
                                                 m_view_start,
                                                 m_view_end,
                                                 0.0,
                                                 (double)getWidth());
    if (is_in_range((double)event.x,markerx-5.0,markerx+5.0)==true)
        return ha_attackmarker;
    if (get_active_time_range().isValid()==false)
        return ha_none;
    if (m_thumb!=nullptr)
    {
        double soundlen=m_thumb->getTotalLength();
        //double selx0=getWidth()/soundlen*get_active_time_range().start();
        //double selx1=getWidth()/soundlen*get_active_time_range().end();
        double selx0=scale_value_from_range_to_range(get_active_time_range().start(),
                                                     m_view_start*soundlen,
                                                     m_view_end*soundlen,
                                                     0.0,
                                                     (double)getWidth());
        double selx1=scale_value_from_range_to_range(get_active_time_range().end(),
                                                         m_view_start*soundlen,
                                                         m_view_end*soundlen,
                                                         0.0,
                                                         (double)getWidth());
        if (is_in_range((double)event.x,selx0-5.0,selx0+5.0)==true)
            return ha_timesel_left;
        if (is_in_range((double)event.x,selx1-5.0,selx1+5.0)==true)
            return ha_timesel_right;
        if (event.mods.isShiftDown()==true && is_in_range((double)event.x,selx0+5.0,selx1-5.0)==true)
            return ha_timesel_all;
    }
    return ha_none;
}


envelope_editor::envelope_editor(Component *parent) : m_parent(parent)
{
    m_view_range=std::make_pair(0.0,1.0);
}

void envelope_editor::paint(Graphics &g, juce::Rectangle<int> r)
{
    if (m_parameter==nullptr)
        return;
    g.saveState();
    g.setColour(m_envelope_colour);
    const float nodesize=8.0f;
    for (int i=0;i<m_parameter->m_env.GetNumNodes()-1;++i)
    {
        const envelope_node& node0=m_parameter->m_env.GetNodeAtIndex(i);
        const envelope_node& node1=m_parameter->m_env.GetNodeAtIndex(i+1);
        float xcor0=scale_value_from_range_to_range(node0.Time,
                                                    m_view_range.first,m_view_range.second,
                                                    0.0,(double)r.getWidth());
        float xcor1=scale_value_from_range_to_range(node1.Time,
                                                    m_view_range.first,m_view_range.second,
                                                    0.0,(double)r.getWidth());
        float ycor0=(1.0-node0.Value)*r.getHeight();
        float ycor1=(1.0-node1.Value)*r.getHeight();
        double value_delta=node1.Value-node0.Value;
        int num_sub_segments=std::max((int)((node1.Time-node0.Time)*r.getWidth()/8),8);
        //g.drawText(String(num_sub_segments),xcor0,10,50,20,Justification::left);
        for (int j=0;j<num_sub_segments;++j)
        {
            float offset0=(xcor1-xcor0)/num_sub_segments*j;
            float offset1=(xcor1-xcor0)/num_sub_segments*(j+1);
            double to_shaping=1.0/(num_sub_segments)*j;
            double shaped=node0.Value+value_delta*get_shaped_value(to_shaping,0,node0.ShapeParam1,0.0);
            float ycor2=(1.0-shaped)*r.getHeight();
            to_shaping=1.0/num_sub_segments*(j+1);
            shaped=node0.Value+value_delta*get_shaped_value(to_shaping,0,node0.ShapeParam1,0.0);
            float ycor3=(1.0-shaped)*r.getHeight();
            g.drawLine(xcor0+offset0,ycor2,xcor0+offset1,ycor3);
        }

        if (m_draw_handles==true)
        {
            g.drawEllipse(xcor0-nodesize/2,ycor0-nodesize/2,nodesize,nodesize,1.0f);
            if (i==m_parameter->m_env.GetNumNodes()-2)
                g.drawEllipse(xcor1-nodesize/2,ycor1-nodesize/2,nodesize,nodesize,1.0f);
        }
    }
    g.setColour(Colours::white);
    String text(m_parameter->m_name);
    if (m_parameter->m_automation_enabled==true)
        text+=" (enabled)";
    g.drawText(text,5,25,300,20,Justification::centredLeft,true);
    g.restoreState();
}

void envelope_editor::on_mouse_event(ISubComponent::mouse_event_type evtype, const MouseEvent &event)
{
    if (m_parameter==nullptr)
        return;
    if (evtype==met_press)
    {
        m_mouse_down=true;
        m_hot_node=-1;
        handle_click(event);
    }
    if (evtype==met_release)
    {
        m_mouse_down=false;
        m_hot_node=-1;
    }
    if (evtype==met_drag)
    {
        m_mouse_down=true;
        handle_drag(event);
    }
    if (evtype==met_move)
    {
        handle_move(event);
    }
    m_mouse_down=true;
}

bool envelope_editor::is_hot()
{
    return m_hot_node>=0 || m_hot_segment>=0;
}

void envelope_editor::handle_click(const MouseEvent &e)
{
    m_hot_node=get_hot_node(e.x,e.y);
    if (m_hot_node<0 && e.mods.isAltDown()==false)
    {
        double time=scale_value_from_range_to_range(e.x,
                                                    0.0,(double)m_parent->getWidth(),
                                                    m_view_range.first,m_view_range.second);
        double value=(1.0/m_parent->getHeight()*e.y);
        m_parameter->m_env.AddNode(envelope_node(time,1.0-value));
        m_parameter->m_env.SortNodes();
        m_hot_node=get_hot_node(e.x,e.y);
        m_dirty=true;
        return;
    }
    m_hot_segment=get_hot_segment(e.x,e.y);
    if (m_hot_segment>=0)
        m_segment_par1=m_parameter->m_env.GetNodeAtIndex(m_hot_segment).ShapeParam1;
    if (m_hot_node>=0 && e.mods.isAltDown()==true)
    {
        if (m_parameter->m_env.GetNumNodes()>2)
        {
            m_parameter->m_env.DeleteNode(m_hot_node);
            m_parameter->m_env.SortNodes();
            m_hot_node=-1;
            m_dirty=true;
        } else
        {
            AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon,
                                             "Error",
                                             "Cannot remove remaining envelope points","OK",
                                             m_parent);
        }
    }
}

std::pair<double,double> envelope_editor::envelope_value_from_y_coord(int y, bool snap)
{
    double value=1.0-(1.0/m_parent->getHeight()*y);
    static const double grid[]={0.0,0.25,0.5,0.75,1.0};
	if (snap == true)
	{
		if (fabs(distance_to_grid(value, grid)) < 0.1)
			value = quantize_to_grid(value, grid);
	}
	value=bound_value(0.0,value,1.0);
    return std::make_pair(value,m_parameter->m_slider_shaping_func(value));
}

void envelope_editor::show_bubble(int x, int y, const envelope_node& node)
{
    double scaledtime=EnvelopeLength()*node.Time;
    double scaledvalue=envelope_value_from_y_coord(y).second;
    m_bubble->showAt({x-50,y,100,20},AttributedString(String::formatted("%.2f %.2f",scaledtime,scaledvalue)),5000);
}

void envelope_editor::handle_drag(const MouseEvent &e)
{
    if (m_hot_segment>=0 && e.mods.isAltDown()==true)
    {
        double delta=1.0/200*e.getDistanceFromDragStartX();
        envelope_node new_node=m_parameter->m_env.GetNodeAtIndex(m_hot_segment);
        new_node.ShapeParam1=bound_value(0.0,m_segment_par1+delta,1.0);
        m_parameter->m_env.SetNode(m_hot_segment,new_node);
        m_dirty=true;
        return;
    }
    if (m_hot_node>=0 && m_mouse_down==true)
    {
        double time=scale_value_from_range_to_range(e.x,
                                                    0.0,(double)m_parent->getWidth(),
                                                    m_view_range.first,m_view_range.second);
        time=bound_value(0.0,time,1.0);
        if (m_hot_node>0 && m_hot_node<m_parameter->m_env.GetNumNodes()-1)
            time=bound_value(m_parameter->m_env.GetNodeAtIndex(m_hot_node-1).Time+0.001,
                             time,
                             m_parameter->m_env.GetNodeAtIndex(m_hot_node+1).Time-0.001);
        double shap_p1=m_parameter->m_env.GetNodeAtIndex(m_hot_node).ShapeParam1;
		bool snap = e.mods.isShiftDown();
		auto values=envelope_value_from_y_coord(e.y,snap);
        envelope_node new_node(time,values.first,shap_p1);
        if (m_hot_node==0)
            new_node=envelope_node(0.0,values.first,shap_p1);
        if (m_hot_node==m_parameter->m_env.GetNumNodes()-1)
            new_node=envelope_node(1.0,values.first,shap_p1);
        m_parameter->m_env.SetNode(m_hot_node,new_node);
        m_parameter->m_env.SortNodes();
        show_bubble(e.x,e.y,new_node);
        m_dirty=true;
    }
}

void envelope_editor::handle_move(const MouseEvent &e)
{
    m_dirty=false;
    int hot_node=get_hot_node(e.x,e.y);
    m_hot_node=hot_node;
    m_hot_segment=get_hot_segment(e.x,e.y);
    if (hot_node>=0)
    {
        m_parent->setMouseCursor(MouseCursor::PointingHandCursor);
        show_bubble(e.x,e.y,m_parameter->m_env.GetNodeAtIndex(hot_node));
    }
    else
    {
        m_parent->setMouseCursor(MouseCursor::NormalCursor);
        m_bubble->setVisible(false);
    }
}

int envelope_editor::get_hot_node(int x, int y)
{
    for (int i=0;i<m_parameter->m_env.GetNumNodes();++i)
    {
        const envelope_node& node=m_parameter->m_env.GetNodeAtIndex(i);
        double xcor=scale_value_from_range_to_range(node.Time,
                                                    m_view_range.first,m_view_range.second,
                                                    0.0,(double)m_parent->getWidth());
        double ycor=m_parent->getHeight()*(1.0-node.Value);
        juce::Rectangle<int> test_rect(xcor-5,ycor-5,10,10);
        if (test_rect.contains(x,y)==true)
            return i;
    }
    return -1;
}

int envelope_editor::get_hot_segment(int x, int y)
{
    for (int i=0;i<m_parameter->m_env.GetNumNodes()-1;++i)
    {
        const envelope_node& node1=m_parameter->m_env.GetNodeAtIndex(i);
        const envelope_node& node2=m_parameter->m_env.GetNodeAtIndex(i+1);
        double xcor1=scale_value_from_range_to_range(node1.Time,
                                                    m_view_range.first,m_view_range.second,
                                                    0.0,(double)m_parent->getWidth());
        double xcor2=scale_value_from_range_to_range(node2.Time,
                                                    m_view_range.first,m_view_range.second,
                                                    0.0,(double)m_parent->getWidth());
        if (x>=xcor1 && x<=xcor2)
        {
            return i;
        }
    }
    return -1;
}

void zoom_scrollbar::mouseDown(const MouseEvent &e)
{
    m_drag_start_x=e.x;
}

void zoom_scrollbar::mouseMove(const MouseEvent &e)
{
    m_hot_area=get_hot_area(e.x,e.y);
    if (m_hot_area==ha_left_edge || m_hot_area==ha_right_edge)
        setMouseCursor(MouseCursor::LeftRightResizeCursor);
    else
        setMouseCursor(MouseCursor::NormalCursor);
}

void zoom_scrollbar::mouseDrag(const MouseEvent &e)
{
    if (m_hot_area==ha_none)
        return;
    if (m_hot_area==ha_left_edge)
    {
        double new_left_edge=1.0/getWidth()*e.x;
        m_start=bound_value(0.0,new_left_edge,m_end-0.01);
        repaint();
    }
    if (m_hot_area==ha_right_edge)
    {
        double new_right_edge=1.0/getWidth()*e.x;
        m_end=bound_value(m_start+0.01,new_right_edge,1.0);
        repaint();
    }
    if (m_hot_area==ha_handle)
    {
        double delta=1.0/getWidth()*(e.x-m_drag_start_x);
        double old_start=m_start;
        double old_end=m_end;
        double old_len=m_end-m_start;
        m_start=bound_value(0.0,m_start+delta,1.0-old_len);
        m_end=bound_value(old_len,m_end+delta,m_start+old_len);
        m_drag_start_x=e.x;
        repaint();
    }
    if (RangeChanged)
        RangeChanged(m_start,m_end);
}

void zoom_scrollbar::paint(Graphics &g)
{
    g.setColour(Colours::darkgrey);
    g.fillRect(0,0,getWidth(),getHeight());
    int x0=getWidth()*m_start;
    int x1=getWidth()*m_end;
    g.setColour(Colours::lightgrey);
    g.fillRect(x0,0,x1-x0,getHeight());
}

zoom_scrollbar::hot_area zoom_scrollbar::get_hot_area(int x, int y)
{
    int x0=getWidth()*m_start;
    int x1=getWidth()*m_end;
    if (is_in_range(x,x0-5,x0+5))
        return ha_left_edge;
    if (is_in_range(x,x1-5,x1+5))
        return ha_right_edge;
    if (is_in_range(x,x0+5,x1-5))
        return ha_handle;
    return ha_none;
}
