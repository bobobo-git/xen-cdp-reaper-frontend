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

#include "jcdp_audio_playback.h"
#include "reaper_plugin_functions.h"

juce_audio_preview::juce_audio_preview(AudioFormatManager* afm) : m_format_manager(afm)
{
    m_buffer.setSize(2,4096,false);
    m_transport_source=new AudioTransportSource();
    m_manager=jcdp::make_unique<AudioDeviceManager>();
    m_manager->initialise(0,2,nullptr,true);
    m_manager->addAudioCallback(this);
    if (m_manager->getCurrentAudioDevice()!=nullptr)
    {
        m_manager->getCurrentAudioDevice()->start(this);
    }
}

juce_audio_preview::~juce_audio_preview()
{
    m_manager->removeAudioCallback(this);
    delete m_transport_source;
    if (m_audio_file.get_file()!=nullptr)
    {
        String fn=m_audio_file.get_file()->getFullPathName();
        m_audio_file.reset();
		remove_file_if_exists(fn);
    }
}

void juce_audio_preview::set_audio_file(String fn)
{
    juce_audio_file tempaudiofile(fn,m_format_manager);
    if (tempaudiofile.get_source()!=nullptr)
    {
        m_mutex.enter();
        m_audio_file=std::move(tempaudiofile);
        m_audio_file.get_source()->setLooping(true);
        m_audio_file.get_source()->prepareToPlay(1024,44100.0);
        m_transport_source->setSource(m_audio_file.get_source(), 0, nullptr, m_audio_file.get_reader()->sampleRate, 2);
        m_transport_source->setLooping(true);
        m_transport_source->prepareToPlay(1024,44100.0);
        m_transport_source->start();
        m_mutex.exit();
    }
}

void juce_audio_preview::audioDeviceIOCallback(const float **, int, float **outputChannelData, int numOutputChannels, int numSamples)
{
    ScopedLock locker(m_mutex);
    if ((numOutputChannels!=2) || (m_audio_file.get_reader()==nullptr) || (m_is_playing==false))
    {
        for (int i=0;i<numOutputChannels;++i)
            for (int j=0;j<numSamples;++j)
                outputChannelData[i][j]=0.0f;
        return;
    }
    AudioSourceChannelInfo info(&m_buffer,0,numSamples);
    m_transport_source->getNextAudioBlock(info);
    if (m_audio_file.get_reader()->numChannels==1)
    {
        for (int i=0;i<numSamples;++i)
        {
            outputChannelData[0][i]=info.buffer->getSample(0,i);
            outputChannelData[1][i]=info.buffer->getSample(0,i);
        }
    }
    if (m_audio_file.get_reader()->numChannels==2)
    {
        for (int i=0;i<numSamples;++i)
        {
            outputChannelData[0][i]=info.buffer->getSample(0,i);
            outputChannelData[1][i]=info.buffer->getSample(1,i);
        }
    }
}

void juce_audio_preview::seek(double seconds)
{
    ScopedLock locker(m_mutex);
    if (m_audio_file.get_reader()!=nullptr)
    {
        m_transport_source->setPosition(seconds);
    }
}

void juce_audio_preview::start()
{
    m_is_playing=true;
}

void juce_audio_preview::stop()
{
    m_is_playing=false;
}

void juce_audio_preview::set_volume(double gain)
{
    if (m_transport_source)
    {
        m_transport_source->setGain(gain);
    }
}

reaper_audio_preview::reaper_audio_preview() : m_mutex(&m_prev_reg)
{
    m_prev_reg.loop=true;
    m_prev_reg.curpos=0.0;
    m_prev_reg.m_out_chan=0;
    m_prev_reg.preview_track=nullptr;
    m_prev_reg.volume=1.0;
    m_prev_reg.src=nullptr;
}

reaper_audio_preview::~reaper_audio_preview()
{
    stop();
    delete m_src;
    remove_file_if_exists(m_filename);
}

bool reaper_audio_preview::is_playing()
{
    return m_is_playing;
}

void reaper_audio_preview::set_audio_file(String fn)
{
    const char* foo=fn.toRawUTF8();
    PCM_source* temp=PCM_Source_CreateFromFile(foo);
    if (temp!=nullptr)
    {
        m_mutex.lock();
        PCM_source* old_src=m_src;
        m_src=temp;
        m_prev_reg.src=temp;
        m_prev_reg.curpos=0.0;
        m_mutex.unlock();
        delete old_src;
        m_filename=fn;
    } else Logger::writeToLog("Could not create PCM_source");
}

void reaper_audio_preview::set_volume(double gain)
{
    m_mutex.lock();
    m_prev_reg.volume=gain;
    m_mutex.unlock();
}

void reaper_audio_preview::start()
{
    if (m_is_playing==true)
        return;
    PlayPreviewEx(&m_prev_reg,1,-1.0);
    m_is_playing=true;
}

void reaper_audio_preview::stop()
{
    if (m_is_playing==false)
        return;
    StopPreview(&m_prev_reg);
    m_is_playing=false;
}

void reaper_audio_preview::seek(double pos)
{
    m_mutex.lock();
    if (m_src!=nullptr)
    {
        m_prev_reg.curpos=pos;
    }
    m_mutex.unlock();
}

double reaper_audio_preview::get_position()
{
    return m_prev_reg.curpos;
}
