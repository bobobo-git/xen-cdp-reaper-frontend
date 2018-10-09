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

#ifndef JCDP_WAVECOMPONENT_H
#define JCDP_WAVECOMPONENT_H

#include "JuceHeader.h"
#include "jcdp_utilities.h"
#include "jcdp_envelope.h"

class zoom_scrollbar : public Component
{
public:
    enum hot_area
    {
        ha_none,
        ha_left_edge,
        ha_right_edge,
        ha_handle
    };
    void mouseDown(const MouseEvent& e);
    void mouseMove(const MouseEvent& e);
    void mouseDrag(const MouseEvent& e);
    void paint(Graphics &g);
    std::function<void(double,double)> RangeChanged;
    std::pair<double,double> get_range() const { return std::make_pair(m_start,m_end); }
private:
    double m_start=0.0;
    double m_end=1.0;
    hot_area m_hot_area=ha_none;
    hot_area get_hot_area(int x, int y);
    int m_drag_start_x=0;
};

struct parameter_info;

class ISubComponent
{
public:
    enum mouse_event_type
    {
        met_press,
        met_release,
        met_move,
        met_drag,
    };
    virtual ~ISubComponent() {}
    virtual void paint(Graphics& g, juce::Rectangle<int>)=0;
    virtual void on_mouse_event(mouse_event_type, const MouseEvent&) {}
    virtual bool is_hot() const { return false; }
    virtual bool is_state_dirty() const=0;
};

class envelope_editor : public ISubComponent
{
public:
    envelope_editor(Component* parent);
    void paint(Graphics &g,juce::Rectangle<int>);
    void on_mouse_event(mouse_event_type, const MouseEvent&);
    bool is_hot();
    parameter_info* get_envelope() const { return m_parameter; }
    bool is_state_dirty() const { return m_dirty; }
    void set_envelope(parameter_info* env) { m_parameter=env; }
    Colour m_envelope_colour;
    bool m_draw_handles=false;
    BubbleMessageComponent* m_bubble=nullptr;
    std::function<double()> EnvelopeLength;
    std::pair<double,double> m_view_range;
private:
    void handle_click(const MouseEvent &e);
    void handle_drag(const MouseEvent &e);
    void handle_move(const MouseEvent &e);
    parameter_info* m_parameter=nullptr;
    Component* m_parent=nullptr;
    bool m_mouse_down=false;
    int get_hot_node(int x, int y);
    int get_hot_segment(int x, int y);
    int m_hot_node=-1;
    int m_hot_segment=-1;
    double m_segment_par1=0.0;
    bool m_dirty=false;
    std::pair<double, double> envelope_value_from_y_coord(int y,bool snap=false);
    void show_bubble(int x, int y, const envelope_node &node);
};

class WaveFormComponent : public Component,
        public Timer,
        public ChangeListener,
        public ChangeBroadcaster,
        public SettableTooltipClient
{
public:
    enum hot_area
    {
        ha_none,
        ha_attackmarker,
        ha_timesel_left,
        ha_timesel_right,
        ha_timesel_all
    };
    enum edit_mode
    {
        em_waveform,
        em_envelope
    };

    WaveFormComponent(bool usetimer);
    ~WaveFormComponent();

    void timerCallback()
    {
        repaint();
    }
    void changeListenerCallback(ChangeBroadcaster* cbc)
    {
        if (cbc==m_thumb)
            repaint();
    }

    void paint(Graphics &g);
    void set_file(String fn);
    void mouseDown(const MouseEvent& event);
    void mouseUp(const MouseEvent& event);
    void mouseMove(const MouseEvent& event);
    void mouseDrag(const MouseEvent& event);
    bool keyPressed(const KeyPress &key);
    void focusLost(FocusChangeType);
    Colour m_waveformcolour;
    File* m_thumb_file;
    FileInputSource* m_thumb_source;
    AudioThumbnail* m_thumb=nullptr;
    void set_show_handle(bool b)
    {
        m_has_handle=b;
        repaint();
    }
    double m_handle_pos=0.2;
    double m_render_elapsed_time=0.0;
    void set_time_range(time_range tr)
    {
        m_waveform_time_range=tr; repaint();
    }
    time_range get_time_range() const { return m_waveform_time_range; }
    void set_parameter(parameter_info* env);
    bool is_dirty() const
    {
        if (m_env_editor!=nullptr && m_env_editor->is_state_dirty()==true)
            return true;
        if (m_which_dirty!=ha_none)
            return true;
        return false;
    }
	void set_parameter_names(StringArray names);
	void set_edit_mode(edit_mode m) { m_edit_mode=m; repaint(); }
    edit_mode get_edit_mode() const { return m_edit_mode; }
    void set_view_range(double,double);
    std::function<void(double)> OnSeekFunc;
    std::function<double(void)> FilePositionFunc;
    std::function<void(time_range)> CutFileFunc;
	std::function<void(String)> EnvelopeChangeRequestedFunc;
private:
    hot_area m_which_dirty=ha_none;
    bool m_has_handle=false;
    time_range m_waveform_time_range;
    time_range m_envelope_time_range;
    time_range m_old_time_range;
    hot_area m_hot_area=ha_none;
    hot_area get_hot_area(const MouseEvent& event);
    std::unique_ptr<envelope_editor> m_env_editor;
    std::unique_ptr<BubbleMessageComponent> m_bubble;
    parameter_info* m_env=nullptr;
    double m_time_sel_drag_start=0.0;
    String m_audio_fn;
    edit_mode m_edit_mode=em_waveform;
    time_range& get_active_time_range()
    {
        if (m_edit_mode==em_envelope)
            return m_envelope_time_range;
        return m_waveform_time_range;
    }
    double m_view_start=0.0;
    double m_view_end=1.0;
	StringArray m_parameter_names;
};


#endif // JCDP_WAVECOMPONENT_H
