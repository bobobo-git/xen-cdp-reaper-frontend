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

#include "jcdp_utilities.h"
//#define REAPERAPI_DECL
#include "reaper_plugin_functions.h"
#undef min

extern std::unique_ptr<AudioFormatManager> g_format_manager;
extern File g_cdp_binaries_dir;

audio_source_info get_audio_source_info(String fn)
{
    audio_source_info result;
    File file(fn);
    auto reader=std::unique_ptr<AudioFormatReader>(g_format_manager->createReaderFor(file));
    if (reader!=nullptr)
    {
        result.num_channels=reader->numChannels;
        result.m_length_frames=reader->lengthInSamples;
        result.samplerate=reader->sampleRate;
    }
    return result;
}

#define NO_TIMED_CACHE

#ifdef NO_TIMED_CACHE

audio_source_info get_audio_source_info_cached(String fn)
{
    static std::map<String,audio_source_info> s_cache;
    auto found=s_cache.find(fn);
    if (found!=s_cache.end())
        return found->second;
    audio_source_info result;
    File file(fn);
    auto reader=std::unique_ptr<AudioFormatReader>(g_format_manager->createReaderFor(file));
    if (reader!=nullptr)
    {
        result.num_channels=reader->numChannels;
        result.m_length_frames=reader->lengthInSamples;
        result.samplerate=reader->sampleRate;
    }
    s_cache[fn]=result;
    //Logger::writeToLog(String(s_cache.size())+" entries in soundfile info cache");
    return result;
}

#else
class juce_time_t
{
public:
    using time_type=double;
    static time_type get_current_time() { return Time::getMillisecondCounterHiRes(); }
};

audio_source_info get_audio_source_info_cached(String fn)
{
    static timed_cache<String,audio_source_info,juce_time_t> s_cache(1000);
    if (s_cache.contains(fn)==true)
    {
        return s_cache[fn];
    }
    audio_source_info result;
    File file(fn);
    auto reader=std::unique_ptr<AudioFormatReader>(g_format_manager->createReaderFor(file));
    if (reader!=nullptr)
    {
        result.num_channels=reader->numChannels;
        result.m_length_frames=reader->lengthInSamples;
        result.samplerate=reader->sampleRate;
    }
    s_cache.insert(fn,result);
    //Logger::writeToLog(String(s_cache.size())+" entries in soundfile info cache");
    return result;
}
#endif

// shared_ptr is not ideal for these (since the pointers are not likely going to be shared),
// but whatever...

std::shared_ptr<AudioAccessor> make_audio_accessor(MediaItem_Take* take)
{
	return std::shared_ptr<AudioAccessor>(CreateTakeAudioAccessor(take), [](AudioAccessor* ac)
	{ 
		if (ac != nullptr) 
			DestroyAudioAccessor(ac); 
	});
}

std::shared_ptr<AudioAccessor> make_audio_accessor(MediaTrack* track)
{
	return std::shared_ptr<AudioAccessor>(CreateTrackAudioAccessor(track), [](AudioAccessor* ac)
	{
		if (ac != nullptr)
			DestroyAudioAccessor(ac);
	});
}

std::pair<String, String> pre_process_file_with_reaper_api(MediaItem_Take* take, time_range tr, double gain, bool makemono)
{
	if (take != nullptr)
	{
		PCM_source* src = (PCM_source*)GetSetMediaItemTakeInfo(take, "P_SOURCE", nullptr);
		auto accessor = make_audio_accessor(take);
		
		if (accessor != nullptr && src!=nullptr)
		{
			char accessor_hash[129];
			memset(accessor_hash, 0, 129);
			GetAudioAccessorHash(accessor.get(), accessor_hash);
			char projpathbuf[4096];
			GetProjectPath(projpathbuf, 4096);
			if (strlen(projpathbuf) == 0)
				return std::make_pair(String(), String());
			String outfilename;
			if (tr.isValid() == true)
			{
				String timerangehash;
				size_t trhash = combine_hashes(tr.start(),tr.end());
				timerangehash = String((int64)trhash);
				outfilename = String(projpathbuf) + "/" + c_file_prefix + String(accessor_hash) + "_" + timerangehash + ".wav";
			} else
				outfilename=String(projpathbuf) + "/" + c_file_prefix + String(accessor_hash) + ".wav";
			if (does_file_exist(outfilename) == true && fuzzy_compare(1.0,gain)==true)
				return std::make_pair(outfilename, String());
			//readbg() << accessor_hash << "\n";
			double accessor_len = GetAudioAccessorEndTime(accessor.get());
			if (tr.isValid() == true)
				accessor_len = tr.length();
			std::unique_ptr<PCM_sink> sink;
			char cfg[] = { 'e','v','a','w', 32, 0 };
			int outnumchans = src->GetNumChannels();
			//if (makemono == true)
			//	outnumchans = 1;
			int outsamplerate = src->GetSampleRate();
			sink = std::unique_ptr<PCM_sink>(PCM_Sink_Create(outfilename.toRawUTF8(),
				cfg, sizeof(cfg), outnumchans, outsamplerate, true));
			if (sink != nullptr)
			{
				//readbg() << "starting write...\n";
				int64_t numsamplestowrite = accessor_len*outsamplerate;
				double counter = 0.0;
				if (tr.isValid() == true)
					counter = tr.start();
				double source_end = counter + accessor_len;
				int diskbufsize = 32768;
				std::vector<double> disk_in_buf(diskbufsize*outnumchans);
				split_buffer<double> disk_out_buf(diskbufsize, outnumchans);
				//readbg() << counter << " " << source_end << "\n";
				while (counter < source_end)
				{
					int samples_to_read = std::min(int64_t(outsamplerate*(source_end - counter)), int64_t(diskbufsize));
					GetAudioAccessorSamples(accessor.get(), outsamplerate, outnumchans, counter, samples_to_read, disk_in_buf.data());
					disk_out_buf.init_from_interleaved(disk_in_buf, [gain](double x, size_t) { return gain*x; });
					sink->WriteDoubles(disk_out_buf.get(), samples_to_read, outnumchans, 0, 1);
					counter += (double)diskbufsize / outsamplerate;
				}
				//readbg() << "ended write\n";
				return std::make_pair(outfilename, String());
			}
		}

	}

	return std::make_pair(String(), String());
}

String preprocess_file(String infn, time_range tr, double gain, bool makemono)
{
    File file(infn);
    auto reader=std::unique_ptr<AudioFormatReader>(g_format_manager->createReaderFor(file));
    if (reader!=nullptr)
    {
        WavAudioFormat outformat;
        String outfn=String("C:/ssd_tests/cdp/chooser_koe2/")+c_file_prefix+"juce_audio_write_test.wav";
        remove_file_if_exists(outfn);
        File outfile(outfn);
        int numoutchans=reader->numChannels;
        int numinchans=reader->numChannels;
        uint64_t file_start=0;
        uint64_t file_end=reader->lengthInSamples;
        if (tr.isValid()==true)
        {
            file_start=reader->sampleRate*tr.start();
            file_end=reader->sampleRate*tr.end();
        }
        if (makemono==true)
            numoutchans=1;
        OutputStream* outstream(outfile.createOutputStream());
        std::unique_ptr<AudioFormatWriter> afwriter(outformat.createWriterFor(outstream,reader->sampleRate,numoutchans,32,StringPairArray(),0));
        if (afwriter!=nullptr)
        {
            const int buffer_size=65536;
            AudioSampleBuffer abuf;
            abuf.setSize(reader->numChannels,buffer_size,false);
            uint64_t counter=file_start;
            while (counter<file_end)
            {
                int samples_to_process=std::min((uint64_t)buffer_size,file_end-counter);
                if (numinchans==numoutchans)
                    reader->read(&abuf,0,samples_to_process,counter,true,true);
                if (fuzzy_compare(gain,1.0)==false)
                {
                    abuf.applyGain(gain);
                }
                afwriter->writeFromAudioSampleBuffer(abuf,0,samples_to_process);
                counter+=buffer_size;
            }
            return outfn;
        } else Logger::writeToLog("Could not create audio writer");
    }
    return String();
}

std::pair<String,uint32_t> run_process(std::initializer_list<String> args,int maxwait)
{
    ChildProcess process;
    StringArray procargs;
    for (auto& e : args)
        procargs.add(std::move(e));
    process.start(procargs);
    process.waitForProcessToFinish(maxwait);
    return std::make_pair(process.readAllProcessOutput(),process.getExitCode());
}

process_future run_process_async(std::initializer_list<String> args, int maxwait)
{
    return std::async(std::launch::async,[args,maxwait]()
    {
        return run_process(args,maxwait);
    });
}

std::pair<int,bool> parse_int_from_string(String input,String token_after_int)
{
    StringArray tokens=StringArray::fromTokens(input," \n","\"");
    for (int i=1;i<tokens.size();++i)
    {
        if (tokens[i]==token_after_int)
        {
            int value=tokens[i-1].getIntValue();
            return std::make_pair(value,true);
        }
    }
    return std::make_pair(0,false);
}

std::pair<int,String> count_wave_cycles(String fn)
{
    static std::map<String,int> s_cache;
    if (fn=="<reset>")
    {
        s_cache.clear();
        return std::make_pair(0,String());
    }
    if (s_cache.count(fn)>0)
    {
        return std::make_pair(s_cache[fn],String());
    }
    auto process_result=run_process({g_cdp_binaries_dir.getFullPathName()+"/distort",
                                    "cyclecnt",
                                    fn});
    if (process_result.second==0)
    {
        auto value=parse_int_from_string(process_result.first,"cycles");
        if (value.second==true)
        {
            s_cache[fn]=value.first;
            return std::make_pair(value.first,String());
        }
    }
    return std::make_pair(0,"Error running CDP Distort Cyclecnt");
}


int readbgbuf::overflow(int c) {
    if (c != traits_type::eof()) {
        char ch[2] = { traits_type::to_char_type(c), 0 };
        ShowConsoleMsg(ch);
    }
    return c;
}

std::streamsize readbgbuf::xsputn(const char *buffer, std::streamsize n) {
    std::string buf(buffer, buffer + n);
    ShowConsoleMsg(buf.c_str());
    return n;
}

ReaperTakeAccessorWrapper::ReaperTakeAccessorWrapper(MediaItem_Take* take)
	: m_take(take)
{
	memset(&m_hash, 0, 128);
	if (m_take != nullptr)
	{
		m_accessor=CreateTakeAudioAccessor(m_take);
		update_hash();
	}
}

ReaperTakeAccessorWrapper::~ReaperTakeAccessorWrapper()
{
	if (m_accessor != nullptr)
	{
		DestroyAudioAccessor(m_accessor);
	}
}

ReaperTakeAccessorWrapper::ReaperTakeAccessorWrapper(ReaperTakeAccessorWrapper&& other)
	: m_take(other.m_take), m_accessor(other.m_accessor)
{
	std::copy(std::begin(other.m_hash), std::end(other.m_hash), std::begin(m_hash));
	other.m_take = nullptr;
	other.m_accessor = nullptr;
}

void ReaperTakeAccessorWrapper::update_hash()
{
	if (m_accessor != nullptr)
	{
		GetAudioAccessorHash(m_accessor, m_hash);
	}
}

bool ReaperTakeAccessorWrapper::has_changed()
{
	if (m_accessor != nullptr)
	{
		char temp[128];
		memset(temp, 0, 128);
		GetAudioAccessorHash(m_accessor, temp);
		return !std::equal(std::begin(m_hash), std::end(m_hash), std::begin(temp));
	}
	return false;
}