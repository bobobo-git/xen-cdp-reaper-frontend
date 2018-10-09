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

#ifndef JCDP_AUDIO_PLAYBACK_H
#define JCDP_AUDIO_PLAYBACK_H

#include <atomic>
#include <memory>
#include "jcdp_utilities.h"
#include "reaper_plugin.h"

#ifndef WIN32
#include <pthread.h>
#endif

#ifdef WIN32
class jcdp_mutex
{
public:
    jcdp_mutex(preview_register_t* preg) : m_preg(preg)
    {
        InitializeCriticalSection(&m_preg->cs);
    }
    ~jcdp_mutex()
    {
        DeleteCriticalSection(&m_preg->cs);
    }
    void lock()
    {
        EnterCriticalSection(&m_preg->cs);
    }
    void unlock()
    {
        LeaveCriticalSection(&m_preg->cs);
    }
    preview_register_t* m_preg=nullptr;
};
#else
class jcdp_mutex
{
public:
    jcdp_mutex(preview_register_t* preg) : m_preg(preg)
    {
        pthread_mutexattr_t mta;
        pthread_mutexattr_init(&mta);
        pthread_mutexattr_settype(&mta, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&m_preg->mutex, &mta);
    }
    ~jcdp_mutex()
    {
        pthread_mutex_destroy(&m_preg->mutex);
    }
    void lock()
    {
        pthread_mutex_lock(&m_preg->mutex);
    }
    void unlock()
    {
        pthread_mutex_unlock(&m_preg->mutex);
    }
    preview_register_t* m_preg=nullptr;
};
#endif

class IJCDPreviewPlayback
{
public:
    virtual ~IJCDPreviewPlayback() {}
    virtual bool is_playing()=0;
    virtual void start()=0;
    virtual void stop()=0;
    virtual void seek(double)=0;
    virtual double get_position()=0;
    virtual void set_audio_file(String)=0;
    virtual void set_volume(double gain)=0;
	virtual bool is_looped() = 0;
	virtual void set_looped(bool b) = 0;
	std::function<void(void)> OnFileEnd;
};

class reaper_audio_preview : public IJCDPreviewPlayback
{
public:
    reaper_audio_preview();
    ~reaper_audio_preview();
    bool is_playing();
    void start();
    void stop();
    void seek(double);
    double get_position();
    void set_audio_file(String);
    void set_volume(double gain);
	bool is_looped() { return m_looped; }
	void set_looped(bool b) 
	{ 
		m_mutex.lock();
		m_looped = b;
		m_prev_reg.loop = b;
		m_mutex.unlock();
	}
	
private:
	bool m_looped = true;
	PCM_source* m_src=nullptr;
    preview_register_t m_prev_reg;
    jcdp_mutex m_mutex;
    std::atomic<bool> m_is_playing={false};
    String m_filename;
};

class juce_audio_file
{
public:
    juce_audio_file() {}
    juce_audio_file(String fn,AudioFormatManager* mgr)
    {
        m_file=new File(fn);
        m_reader=mgr->createReaderFor(*m_file);
        if (m_reader!=nullptr)
        {
            m_source=new AudioFormatReaderSource(m_reader,true);
        } else Logger::writeToLog("Could not create audio file of "+fn);
    }
    ~juce_audio_file()
    {
        delete m_source;
        delete m_file;
    }
    juce_audio_file(const juce_audio_file&)=delete;
    juce_audio_file& operator=(const juce_audio_file&)=delete;
    juce_audio_file(juce_audio_file&& other)
    {
        m_file=other.m_file;
        other.m_file=nullptr;
        m_source=other.m_source;
        other.m_source=nullptr;
        m_reader=other.m_reader;
        other.m_reader=nullptr;
    }
    juce_audio_file& operator=(juce_audio_file&& other)
    {
        std::swap(m_file,other.m_file);
        std::swap(m_source,other.m_source);
        std::swap(m_reader,other.m_reader);
        return *this;
    }
    AudioFormatReaderSource* get_source() const { return m_source; }
    AudioFormatReader* get_reader() const { return m_reader; }
    File* get_file() const { return m_file; }
    void reset()
    {
        delete m_source;
        m_source=nullptr;
        delete m_file;
        m_file=nullptr;
    }

private:
    File* m_file=nullptr;
    AudioFormatReader* m_reader=nullptr;
    AudioFormatReaderSource* m_source=nullptr;
};

class juce_audio_preview : public IJCDPreviewPlayback, public AudioIODeviceCallback
{
public:
    juce_audio_preview(AudioFormatManager* afm);
    ~juce_audio_preview();
    void set_audio_file(String fn);

    void audioDeviceIOCallback(const float** inputChannelData,
                               int numInputChannels,
                               float** outputChannelData,
                               int numOutputChannels,
                               int numSamples);
    void audioDeviceAboutToStart(AudioIODevice*) { }
    void audioDeviceStopped() { }
    void seek(double seconds);
    double get_position() { return m_transport_source->getCurrentPosition(); }
    bool is_playing() { return m_is_playing; }
    void start();
    void stop();
    void set_volume(double gain);
	bool is_looped() { return m_looped; }
	void set_looped(bool b)
	{
		m_mutex.enter();
		m_looped = b;
		m_mutex.exit();
	}
private:
	bool m_looped = true;
	AudioTransportSource* m_transport_source=nullptr;
    AudioSampleBuffer m_buffer;
    CriticalSection m_mutex;
    std::atomic<bool> m_is_playing={false};
    AudioFormatManager* m_format_manager=nullptr;
    std::unique_ptr<AudioDeviceManager> m_manager;
    juce_audio_file m_audio_file;
};

#endif // JCDP_AUDIO_PLAYBACK_H
