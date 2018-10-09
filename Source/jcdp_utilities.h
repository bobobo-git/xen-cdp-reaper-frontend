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

#ifndef JCDP_UTILITIES_H
#define JCDP_UTILITIES_H

#include <functional>
#include <unordered_set>
#include <map>
#include <vector>
#include <memory>
#include <future>
#include "JuceHeader.h"
#include "jcdp_envelope.h"

#ifndef WIN32
void makeWindowFloatingPanel(Component *aComponent);
#endif

class MediaItem_Take;
class AudioAccessor;

static const char* c_file_prefix="__CDPTEMP934125__";

static inline bool fuzzy_is_zero(double d)
{
        return fabs(d) <= 0.000000000001;
}

static inline bool fuzzy_compare(double p1, double p2)
{
    return (fabs(p1 - p2) * 1000000000000. <= std::min(fabs(p1), fabs(p2)));
}

class time_range
{
public:
    time_range() : m_valid(false), m_start(0.0), m_end(0.0) {}
    time_range(double start, double end) : m_start(start), m_end(end), m_valid(true) {}
    double start() const { return m_start; }
    double end() const { return m_end; }
    double length() const
    {
        if (m_valid==false) return 0.0;
        return m_end-m_start;
    }
    bool isValid() const { return m_valid; }
    bool operator==(const time_range& rhs)
    {
        return m_valid==rhs.m_valid && m_start==rhs.m_start && m_end==rhs.m_end;
    }
    bool operator!=(const time_range& rhs)
    {
        return !(*this==rhs);
    }
private:
    double m_start;
    double m_end;
    bool m_valid;
};

inline String make_valid_id_string(String in)
{
	in = in.replaceCharacter(' ', '_');
	return in.replaceCharacter('/', '-');
}

template<typename T>
inline size_t hash_helper(const T& x)
{
	return std::hash<T>()(x);
}

template<typename T>
inline void combine_hashes_helper(size_t& seed, const T& x)
{
	seed ^= hash_helper(x) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

template<typename T, typename... Args>
inline void combine_hashes_helper(size_t& seed, const T& x, Args&&... args)
{
	seed ^= hash_helper(x) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
	combine_hashes_helper(seed, std::forward<Args>(args)...);
}

template<typename... Args>
inline size_t combine_hashes(Args&&... args)
{
	size_t seed = 0;
	combine_hashes_helper(seed, std::forward<Args>(args)...);
	return seed;
}

inline void remove_file_if_exists(String path, bool ignore_prefix=false)
{
    if (path.isEmpty()==true)
        return;
    if (path.contains(c_file_prefix)==true || ignore_prefix==true)
    {
        File file(path);
        if (file.exists()==true)
            file.deleteFile();
    } else
        Logger::writeToLog("Attempt to delete file that wasn't created by CDP frontend : "+path);
}

inline bool does_file_exist(String path)
{
    File file(path);
    return file.exists();
}

inline bool do_all_files_exist(StringArray paths)
{
    for (auto& e : paths)
    {
        File file(e);
        if (file.exists()==false)
            return false;
    }
    return true;
}

std::pair<String,uint32_t> run_process(std::initializer_list<String> args, int maxwait=10000);

using process_future=std::future<std::pair<String,uint32_t>>;

process_future run_process_async(std::initializer_list<String> args, int maxwait=10000);

std::pair<int, String> count_wave_cycles(String fn);

struct my_rectangle
{
    my_rectangle(int x1_, int y1_, int x2_, int y2_) :
        x1(x1_), x2(x2_), y1(y1_), y2(y2_) {}
    int x1=0; int x2=0; int y1=0; int y2=0;
};

class LambdaPositioner
{
public:
    using BoundsFunc=std::function<my_rectangle()>;
    struct entry
    {
        entry() {}
        entry(Component* c, BoundsFunc f, int p) : m_comp(c), m_func(f), m_priority(p) {}
        Component* m_comp=nullptr;
        BoundsFunc m_func;
        int m_priority=100;
    };
    void add(Component* comp,BoundsFunc f, int priority=100)
    {
        m_is_sorted=false;
        m_entries.push_back({comp,f,priority});
    }
    void remove(Component* comp)
    {
        m_is_sorted=false;
        auto predicate=[comp](const entry& c) { return comp==c.m_comp; };
        m_entries.erase(std::remove_if(std::begin(m_entries), std::end(m_entries), predicate ), std::end(m_entries) );
    }
    void remove_set(std::unordered_set<Component*> comps)
    {
        m_is_sorted=false;
        auto predicate=[comps](const entry& c) { return comps.count(c.m_comp)>0; };
        m_entries.erase(std::remove_if(std::begin(m_entries), std::end(m_entries), predicate ), std::end(m_entries) );
    }
    void execute()
    {
        if (m_is_sorted==false)
        {
            std::sort(m_entries.begin(),m_entries.end(),[](const entry& a, const entry& b)
            { return a.m_priority<b.m_priority; });
            m_is_sorted=true;
        }
        for (auto& e : m_entries)
        {
            my_rectangle r=e.m_func();
            e.m_comp->setBounds(r.x1,r.y1,r.x2-r.x1,r.y2-r.y1);
        }
    }
private:
    std::vector<entry> m_entries;
    bool m_is_sorted=false;
};

class readbgbuf : public std::streambuf
{
public:
    int overflow(int c);
    std::streamsize xsputn(const char *buffer, std::streamsize n);
};

class readbg : public std::ostream
{
    readbgbuf buf;
public:
    readbg():std::ostream(&buf) { }
};

class file_cleaner
{
public:
    file_cleaner() {}
    ~file_cleaner()
    {
        for (auto& e : m_files)
            remove_file_if_exists(e.filename,e.ignore_safety_check);
    }
    void add(String fn, bool ignore_safety_check=false)
    {
        m_files.push_back({fn,ignore_safety_check});
    }
    void add_multiple(StringArray arr, bool ignore_safety_check=false)
    {
        for (auto& e : arr)
            m_files.push_back({e,ignore_safety_check});
    }
private:
    struct entry
    {
        entry() {}
        entry(String fn, bool ignoresafety) : filename(fn), ignore_safety_check(ignoresafety) {}
        String filename;
        bool ignore_safety_check=false;
    };
    std::vector<entry> m_files;
};

struct run_at_scope_end
{
    run_at_scope_end() {}
    run_at_scope_end(std::function<void(void)> f) : m_f(f) {}
    ~run_at_scope_end()
    {
        if (m_f)
            m_f();
    }
    std::function<void(void)> m_f;
};

template<typename T>
inline T bound_value(const T& minval,const T& val, const T& maxval)
{
    return std::max(minval,std::min(maxval,val));
}

template<typename T>
inline bool is_in_range(T val, T start, T end)
{
    return val>=start && val<=end;
}

template<typename T, typename U>
inline U from_range(T input, U insiderange, U outsiderange, T start, T end)
{
    if (input>=start && input<=end)
        return insiderange;
    return outsiderange;
}

inline double scale_value_from_range_to_range(double v, double inputmin, double inputmax, double outputmin, double outputmax)
{
    double range1=inputmax-inputmin;
    double range2=outputmax-outputmin;
    return outputmin+(range2/range1)*(v-inputmin);
}


struct audio_source_info
{
    int num_channels=0;
    int samplerate=0;
    int64_t m_length_frames=0;
    double get_length_seconds() const
    {
        if (m_length_frames==0 || samplerate==0)
            return 0.0;
        return (double)m_length_frames/samplerate;
    }
};

audio_source_info get_audio_source_info(String fn);
audio_source_info get_audio_source_info_cached(String fn);

class ReaperTakeAccessorWrapper
{
public:
	ReaperTakeAccessorWrapper() {}
	ReaperTakeAccessorWrapper(MediaItem_Take* take);
	~ReaperTakeAccessorWrapper();
	bool has_changed();
	ReaperTakeAccessorWrapper(const ReaperTakeAccessorWrapper&) = delete;
	ReaperTakeAccessorWrapper& operator=(const ReaperTakeAccessorWrapper&) = delete;
	ReaperTakeAccessorWrapper(ReaperTakeAccessorWrapper&& other);
private:
	MediaItem_Take* m_take = nullptr;
	AudioAccessor* m_accessor = nullptr;
	char m_hash[128];
	void update_hash();
};

namespace jcdp
{

template<typename T, typename... Args>
inline std::unique_ptr<T> make_unique(Args&&... args)
{
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

}

inline StringArray make_string_array(std::initializer_list<String> list)
{
    StringArray result;
    for (auto& e: list)
        result.add(e);
    return result;
}

template<typename T>
inline std::unordered_set<T> set_from_vector(std::vector<T> vec)
{
    std::unordered_set<T> result;
    for (auto& e : vec)
        result.insert(e);
    return result;
}

class JCDP_Commands : public ApplicationCommandTarget
{
public:
    using command_func_t=std::function<void(void)>;
    struct entry_t
    {
        entry_t() : command_info(0) {}
        command_func_t command_func;
        ApplicationCommandInfo command_info;
    };
    JCDP_Commands(Component* comp) : m_target_component(comp)
    {

    }

    void add_command(String desc, command_func_t func, KeyPress shortcut)
    {
        static int counter=100;
        ApplicationCommandInfo cmdinfo(counter);
        cmdinfo.shortName=desc;
        cmdinfo.description=desc;
        cmdinfo.flags=0;
        cmdinfo.setActive(true);
        Array<KeyPress> presses;
        presses.add(shortcut);
        cmdinfo.defaultKeypresses=presses;
        entry_t entry;
        entry.command_func=func;
        entry.command_info=cmdinfo;
        m_entries.push_back(entry);
        //m_cmd_manager.registerCommand(cmdinfo);
        m_cmd_manager.registerAllCommandsForTarget(this);
        ++counter;
    }
    ApplicationCommandTarget* getNextCommandTarget()
    {
        Logger::writeToLog("getnextcommandtarget");
        return this;
    }
    void getAllCommands(Array<CommandID> &commands)
    {
        Logger::writeToLog("getallcommands");
        for (auto& e : m_entries)
            commands.add(e.command_info.commandID);
    }
    void getCommandInfo(CommandID commandID, ApplicationCommandInfo &result)
    {
        Logger::writeToLog("getcommandinfo");
        for (auto& e : m_entries)
            if (e.command_info.commandID==commandID)
            {
                result=e.command_info;
                Logger::writeToLog("found");
                return;
            }
    }
    bool perform(const InvocationInfo &info)
    {
        Logger::writeToLog("perform");
        for (auto& e : m_entries)
        {
            if (e.command_info.commandID==info.commandID)
            {
                e.command_func();
                return true;
            }
        }
        return false;
    }

private:
    std::vector<entry_t> m_entries;
    ApplicationCommandManager m_cmd_manager;
    Component* m_target_component;
};

template<typename U,typename F,typename... Args>
inline auto checkedcall(U* ptr,F f,Args... args) -> decltype(std::mem_fn(f)(ptr,args...))
{
    if (ptr!=nullptr)
        return std::mem_fn(f)(ptr,args...);
    return decltype(std::mem_fn(f)(ptr,args...))();
}

template<typename T, typename Cont>
inline double distance_to_grid(const T& x, const Cont& grid)
{
    auto t1=std::lower_bound(std::begin(grid),std::end(grid),x);
    if (t1!=std::end(grid))
    {
        return grid_value(*t1)-grid_value(x);
    }
    return 1.0;
}

template<typename T,typename Grid>
inline double quantize_to_grid(T x, const Grid& g, double amount=1.0)
{
    auto t1=std::lower_bound(std::begin(g),std::end(g),x);
    if (t1!=std::end(g))
    {
        auto t0=std::begin(g);
        if (t1>std::begin(g))
            t0=t1-1;
        const double gridvalue = fabs(grid_value(*t0) - grid_value(x)) < fabs(grid_value(*t1) - grid_value(x)) ? grid_value(*t0) : grid_value(*t1);
        return x + amount * (gridvalue - x);

    }
	const double last_val = *(std::end(g) - 1);
	const double diff=grid_value(last_val-grid_value(x));
    return x+diff*amount;
}

template<typename F, typename... Args>
// F must be a Callable with operator bool to determine if the call can be made
inline auto safe_call(F&& f,Args&&... args)
{
	if (f)
		return f(std::forward<Args>(args)...);
	using result_type = typename std::result_of<F(Args...)>::type;
	return result_type();
}

template<typename TKey, typename TValue, typename TTime>
class timed_cache
{
public:
    timed_cache(int max_entries=5) : m_max_entries(max_entries) {}
    void insert(TKey k, TValue v)
    {
        m_map[k]=cache_entry(v,TTime::get_current_time());
        if (m_map.size()>m_max_entries)
            purge_oldest();
    }
    bool contains(const TKey& k) const
    {
        return m_map.find(k)!=m_map.end();
    }
    const TValue& operator[](const TKey& k)
    {
        m_map[k].m_last_used=TTime::get_current_time();
        return m_map[k].m_x;
    }
private:
    void purge_oldest()
    {
        auto min_found=m_map.begin()->second.m_last_used;
        for (auto& e : m_map)
            if (e.second.m_last_used<min_found)
            {
                Logger::writeToLog("oldest found : "+e.first);
                m_map.erase(e.first);
                break;
            }
    }
    struct cache_entry
    {
        cache_entry() {}
        cache_entry(TValue x, typename TTime::time_type time) : m_x(x), m_last_used(time) {}
        TValue m_x;
        typename TTime::time_type m_last_used=0.0;
    };
    std::map<TKey,cache_entry> m_map;
    int m_max_entries=5;
};

// Hack needed to work around Juce's getExitCode bug in OS-X...
inline String cdp_process_result(ChildProcess& proc)
{
#ifdef WIN32
    if (proc.getExitCode()==0)
        return String();
    return proc.readAllProcessOutput();
#else
    String output=proc.readAllProcessOutput();
    if (output.contains("ERROR") || output.contains("Application doesn't work"))
        return output;
    return String();
#endif
}

class child_processes
{
public:
    using ChildProcessPtr=std::unique_ptr<ChildProcess>;
    child_processes() {}
    void set_spin_sleep_amount(int ms) { m_spin_sleep=ms; }
    void add_and_start_task(StringArray arguments)
    {
        m_processes.push_back(jcdp::make_unique<ChildProcess>());
        m_processes.back()->start(arguments);
    }
    void add_task(StringArray arguments)
    {
        m_processes.push_back(jcdp::make_unique<ChildProcess>());
        m_arguments.push_back(arguments);
    }
    String process_sequentially(int max_wait)
    {
        for (int i=0;i<m_processes.size();++i)
        {
            m_processes[i]->start(m_arguments[i]);
            m_processes[i]->waitForProcessToFinish(max_wait);
            if (m_processes[i]->getExitCode()!=0)
                return m_processes[i]->readAllProcessOutput();
        }
        return String();
    }

    // All must succeed, otherwise returns string with the output of some failed process
    String wait_for_finished(int wait_ms)
    {
        int success_count=0;
        int count=m_processes.size();
        double t0=Time::getMillisecondCounterHiRes();
        while (1)
        {
            for (int i=0;i<m_processes.size();++i)
            {
                if (m_processes[i]->isRunning()==false)
                {
                    String tempresult=cdp_process_result(*m_processes[i]);
                    if (tempresult.isEmpty()==true)
                        ++success_count;
                    else
                        return tempresult;
                    m_processes.erase(m_processes.begin()+i);
                }
            }
            if (success_count==count)
                return String();
            double t1=Time::getMillisecondCounterHiRes();
            if (t1-t0>wait_ms)
                return "Wait time exceeded";
            Thread::sleep(m_spin_sleep);
        }
        return "Unknown error";
    }

private:
    std::vector<ChildProcessPtr> m_processes;
    std::vector<StringArray> m_arguments;
    int m_spin_sleep=10;
};

template<typename T>
class split_buffer
{
public:
	split_buffer() {}
	split_buffer(size_t length, size_t chans) : m_buf(length*chans), m_buf_ptrs(chans)
	{
		init_buf_pointers();
	}
	T** get()
	{
		return m_buf_ptrs.data();
	}
	template<typename U,typename F>
	void init_from_interleaved(const std::vector<U>& source, F&& f)
	{
		T** buffers = get();
		const size_t chans = m_buf_ptrs.size();
		for (size_t i = 0; i < chans; ++i)
		{
			for (size_t j = 0; j < source.size() / chans; ++j)
			{
				buffers[i][j] = f(source[j * chans + i] , i);
			}
		}
	}
	template<typename U>
	void init_from_interleaved(const std::vector<U>& source)
	{
		init_from_interleaved(source, [](const U& x, size_t) { return T(x); });
	}
private:
	void init_buf_pointers()
	{
		for (size_t i = 0; i < m_buf_ptrs.size(); ++i)
			m_buf_ptrs[i] = &m_buf[m_buf.size() / m_buf_ptrs.size()*i];
	}
	std::vector<T> m_buf;
	std::vector<T*> m_buf_ptrs;
};

String preprocess_file(String infn, time_range tr, double gain, bool makemono);
std::pair<String, String> pre_process_file_with_reaper_api(MediaItem_Take* take, time_range tr, double gain, bool makemono);

#ifdef WIN32
#include "Windows.h"
class visit_windows
{
public:
    void operator()(HWND hwnd,std::function<bool(HWND)> visit_func)
    {
        m_cb=visit_func;
        EnumChildWindows(hwnd,(WNDENUMPROC)visit_windows_callback,(LPARAM)this);
    }
private:
    static BOOL visit_windows_callback(HWND hwnd, LPARAM lp)
    {
        visit_windows* this_=(visit_windows*)lp;
        return this_->m_cb(hwnd);
    }
    std::function<bool(HWND)> m_cb;
};

using enum_windows_func=std::function<bool(HWND)>;

static BOOL visit_windows_callback2(HWND hwnd, LPARAM lp)
{
    enum_windows_func* this_=(enum_windows_func*)lp;
    return (*this_)(hwnd);
}

inline void my_visit_windows(HWND hwnd, enum_windows_func visit_func)
{
    EnumChildWindows(hwnd,(WNDENUMPROC)visit_windows_callback2,(LPARAM)&visit_func);
}

template<typename F>
static BOOL visit_windows_callback3(HWND hwnd, LPARAM lp)
{
	F* this_ = (F*)lp;
	return (*this_)(hwnd);
}

template<typename F>
inline void my_visit_windows2(HWND hwnd, F visit_func)
{
	EnumChildWindows(hwnd, (WNDENUMPROC)visit_windows_callback3<F>, (LPARAM)&visit_func);
}

#endif

#endif // JCDP_UTILITIES_H
