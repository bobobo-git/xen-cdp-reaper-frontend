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

#ifndef JCDP_PARAMETER_H
#define JCDP_PARAMETER_H

#include "jcdp_envelope.h"

struct CDP_processor_info;

struct parameter_info
{
    enum WantSpecialNotifications
    {
        none,
        waveformmarker
    };
    parameter_info() {}
    parameter_info(String name, double defval, double minval, double maxval,
                   bool can_automate=false,String cprefix=String(), bool is_skewed=false, double skew_factor=0.0,
                   double step_size=0.05) :
        m_name(name),
        m_default_value(defval),
        m_minimum_value(minval),
        m_maximum_value(maxval),
        m_current_value(defval),
        m_cmd_prefix(cprefix),
        m_can_automate(can_automate),
        m_skewed(is_skewed),
        m_skew(skew_factor),
        m_step(step_size)
    {
        m_env.SetName(m_name);
        m_slider_shaping_func=[minval,maxval](double x)
        {
            return minval+(maxval-minval)*x;
        };
    }
    String m_name;
    String m_cmd_prefix;
    bool m_skewed=false;
    double m_skew=0.0;
    double m_step=0.05;
    double m_current_value=0.0;
    double m_default_value=0.0;
    double m_minimum_value=0.0;
    double m_maximum_value=0.0;
    bool m_can_automate=false;
    bool m_automation_enabled=false;
    WantSpecialNotifications m_notifs=none;
    breakpoint_envelope m_env;
    std::function<double(CDP_processor_info*)> m_envelope_time_scaling_func;
    std::function<std::pair<String,bool>(parameter_info*)> m_cmd_arg_formatter;
    std::function<double(double)> m_slider_shaping_func;
};

struct CDP_processor_info
{
    CDP_processor_info() {}
    CDP_processor_info(String title, String mainp, String subp, String mode,
                       bool spectral, bool changesduration, bool mono_only=false)
        : m_title(title), m_main_program(mainp), m_sub_program(subp), m_mode(mode),
          m_is_spectral(spectral), m_changes_duration(changesduration), m_mono_only(mono_only)
    {
        m_parameters.push_back({"Pre volume",0.0,-12.0,12.0});
        if (m_is_spectral==true)
        {
            m_parameters.push_back({"FFT Size",1024.0,128.0,8192.0,false,"-c"});
            m_parameters.push_back({"FFT Overlap",3.0,1.0,4.0,false,"-o"});
            m_mono_only=true;
        }
    }
    String m_title;
    String m_main_program;
    String m_sub_program;
    String m_mode;
	String m_pluginname;
	int m_plugin_id = 0;
    bool m_changes_duration=false;
    bool m_is_spectral=false;
    bool m_mono_only=false;
    std::vector<parameter_info> m_parameters;
	bool m_is_dirty = false;
};

#endif // JCDP_PARAMETER_H
