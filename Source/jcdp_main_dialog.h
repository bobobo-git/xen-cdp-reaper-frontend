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

#ifndef JCDP_MAIN_DIALOG_H
#define JCDP_MAIN_DIALOG_H

#include <random>
#include <vector>
#include <memory>
#include <set>
#include "JuceHeader.h"
#include "jcdp_utilities.h"
#include "jcdp_wavecomponent.h"
#include "jcdp_audio_playback.h"
#include "jcdp_processor.h"



inline bool has_supported_media_type(PCM_source* src)
{
	return true;
	if (strcmp(src->GetType(),"WAVE")==0)
        return true;
    return false;
}

String get_audio_render_path();

String get_temp_audio_file_name(String suffix="wav");

class MediaItem;
class MediaItem_Take;

class CDPListBox : public ListBox, private ListBoxModel, public ChangeBroadcaster
{
public:
    CDPListBox(std::vector<CDP_processor_info>* infos);
    int getNumRows() { return (int)m_infos->size(); }
    void paintListBoxItem(int rowNumber, Graphics &g, int width, int height, bool rowIsSelected);
    void listBoxItemClicked(int row, const MouseEvent &e);
    void selectedRowsChanged (int lastRowSelected);
private:
    std::vector<CDP_processor_info>* m_infos;
};

class parameter_component : public Component,
							private Button::Listener,
							private ComboBox::Listener
{
public:
    parameter_component(parameter_info* ppar);
    void resized();
    void buttonClicked(Button* but);
    parameter_info* m_param=nullptr;
    std::function<void(parameter_info*)> ShowEnvelopeFunc;
    std::function<void(parameter_info*)> EnableEnvelopeFunc;
	std::function<void(void)> StateChangedFunc;
    void update_from_param();
	double get_value_normalized() const;
	double get_value() const;
	double get_value_from_normalized(double x) const;
	void add_slider_listener(Slider::Listener* listener);
private:
    std::unique_ptr<Label> m_name_label;
    std::unique_ptr<Slider> m_param_slider;
    std::unique_ptr<TextButton> m_enable_autom_button;
    std::unique_ptr<TextButton> m_show_autom_button;
	std::unique_ptr<ComboBox> m_combo;
	void comboBoxChanged(ComboBox* combo);
	void update_combo_item_index();
};

// GUI class that also deals with the logic of doing the CDP processings
// probably not the best design but suffices for now

class cdp_main_dialog : public ResizableWindow,
		public Slider::Listener,
		public Button::Listener,
        public ChangeListener,
        public MultiTimer,
		public ComboBox::Listener
{
public:
    cdp_main_dialog(IJCDPreviewPlayback *delegate, std::vector<CDP_processor_info>* proc_infos, 
		KnownPluginList* kplist);
    ~cdp_main_dialog();
    time_range get_input_time_range() const;
    void resized();
    void init_param_components();
#ifdef CDP_VST_ENABLED
	void init_plugin();
#endif
    void change_waveform_listener_params();
    void changeListenerCallback(ChangeBroadcaster* bc);
#ifndef BUILD_CDP_FRONTEND_PLUGIN
	void set_input_file(String fn,MediaItem_Take* take, time_range trange=time_range());
#else
	void set_reaper_take(MediaItem_Take* take);
#endif
	void focusLost(FocusChangeType reason);
	void focusGained(FocusChangeType reason);
	int index_of_named_processor(const String& name) const;
    void closeButtonPressed();
    bool keyPressed(const KeyPress &);
    void userTriedToCloseWindow();
	int getDesktopWindowStyleFlags() const;
    void timerCallback(int id);
    void sliderValueChanged(Slider *slider);
    void sliderDragEnded(Slider* slider);
    void finalize_output_file();
    void update_parameters_from_sliders();
    CDP_processor_info& get_current_processor();
    float getDesktopScaleFactor() const { return m_gui_scale_factor; }
    void buttonClicked(Button* but);
    void toggle_preview();
    void show_menu();
    void import_file();
    void import_item();
    void process_deferred(int delms=500);
    std::pair<StringArray,String> do_pvoc_analysis(StringArray infiles,int wsize, int olap);
    std::pair<String,String> cut_file(String infn, time_range trange, bool excise=false);
    String adjust_file_volume(String infn, double vol);
    String monoize_file(String infn);
    std::pair<StringArray,String> split_multichannel_file(String fn,audio_source_info info, file_cleaner& cleaner);
    std::pair<String,String> merge_split_files(StringArray infiles);
    std::pair<StringArray, String> do_pvoc_resynth(StringArray infiles);

    void process_cdp();
    std::unique_ptr<TextButton> m_import_button;
    std::unique_ptr<TextButton> m_preview_button;
    std::unique_ptr<TextButton> m_menu_button;
    std::unique_ptr<TextButton> m_ok_button;
    std::unique_ptr<TextButton> m_edit_waveform_button;
    std::unique_ptr<TextButton> m_edit_envelope_button;
    std::unique_ptr<WaveFormComponent> m_input_waveform;
    std::unique_ptr<WaveFormComponent> m_output_waveform;
    std::unique_ptr<zoom_scrollbar> m_inwf_scroller;
    
    std::unique_ptr<CDPListBox> m_proc_listbox;
    std::unique_ptr<Label> m_status_label;
	std::unique_ptr<Label> m_auto_render_status_label;
    std::unique_ptr<Component> m_content_comp;
    std::unique_ptr<TooltipWindow> m_tool_tip;
    std::unique_ptr<Slider> m_preview_volume_slider;
    std::vector<std::unique_ptr<parameter_component>> m_param_comps;
    void update_edit_mode_buttons();
    String m_in_fn;
    String m_out_fn;
    IJCDPreviewPlayback* m_audio_delegate=nullptr;
    std::vector<CDP_processor_info>* m_proc_infos;
    bool m_mono_accepted=false;
    LambdaPositioner m_positioner;
    bool m_custom_time_set=false;
    bool m_follow_item_selection=false;
    void init_audio_hw();
    int m_current_processor_index=-1;
    String m_env_bsize;
    String m_last_cut_fn;
    bool m_render_timer_enabled=true;
    void update_status_label();
    //JCDP_Commands m_commands;
    void comboBoxChanged (ComboBox *comboBoxThatHasChanged);
    void save_state();
    void load_state();
private:
	float m_gui_scale_factor = 1.0;
	bool m_state_dirty=false;
    std::set<String> m_finalized_files;
    std::atomic<bool> m_is_processing_cdp{false};
    std::atomic<int> m_task_counter{0};
    std::mutex m_task_counter_mutex;
    void update_status_label_async(String txt);
    void set_auto_render_enabled(bool b);
    std::unique_ptr<ComboBox> m_presets_combo;
	std::unique_ptr<TextButton> m_presets_button;
    bool handle_processing_cancel(file_cleaner& cleaner, StringArray& filenames);
	bool m_large_envelope = false;
	void update_envelope_size();
	MediaItem_Take* m_reaper_take = nullptr;
	ReaperTakeAccessorWrapper m_take_accessor_wrapper;
	void commit_cdp_render();
	void populate_presets_combo(bool keep_current_selection);
	void show_presets_menu();
	void add_preset_from_current_processor_state(String presetname);
	ValueTree m_presets_value_tree;
	void load_presets_file();
	void save_presets_file();
	void update_current_preset();
	int get_max_preset_id();
	KnownPluginList* m_kplist = nullptr;
	AudioPluginInstance* m_plugin_instance=nullptr;
};

#endif // JCDP_MAIN_DIALOG_H
