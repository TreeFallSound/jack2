/*
Copyright (C) 2008 Grame

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include "JackResampler.h"
#include "JackError.h"
#include "JackTime.h"
#include <atomic>
#include <stdio.h>

namespace Jack
{

JackRingBuffer::JackRingBuffer(int size):fRingBufferSize(size)
{
    fRingBuffer = jack_ringbuffer_create(sizeof(jack_default_audio_sample_t) * fRingBufferSize);
    Reset(fRingBufferSize);
}

JackRingBuffer::~JackRingBuffer()
{
    if (fRingBuffer) {
        jack_ringbuffer_free(fRingBuffer);
    }
}

void JackRingBuffer::Reset(unsigned int new_size)
{
    fRingBufferSize = new_size;
    jack_ringbuffer_reset(fRingBuffer);
    jack_ringbuffer_reset_size(fRingBuffer, sizeof(jack_default_audio_sample_t) * fRingBufferSize);
    jack_ringbuffer_read_advance(fRingBuffer, (sizeof(jack_default_audio_sample_t) * new_size/2));
}

namespace {
    std::atomic<uint64_t> gReadFailureCount(0);
    std::atomic<uint64_t> gWriteFailureCount(0);
    std::atomic<jack_time_t> gLastReadFailureReport(0);
    std::atomic<jack_time_t> gLastWriteFailureReport(0);

    static_assert(__atomic_always_lock_free(sizeof(uint64_t), 0), "ring failure counters must be lock-free");

    uint64_t TakeFailureReportCount(std::atomic<uint64_t>& count,
                                    std::atomic<jack_time_t>& last_report)
    {
        count.fetch_add(1, std::memory_order_relaxed);
        jack_time_t now = GetMicroSeconds();
        jack_time_t last = last_report.load(std::memory_order_relaxed);
        if (last == 0 || now - last >= 1000000) {
            if (last_report.compare_exchange_strong(last, now,
                                                    std::memory_order_relaxed,
                                                    std::memory_order_relaxed)) {
                return count.exchange(0, std::memory_order_relaxed);
            }
        }
        return 0;
    }
}

uint64_t JackRingBuffer::ReadFailureReportCount()
{
    return TakeFailureReportCount(gReadFailureCount, gLastReadFailureReport);
}

uint64_t JackRingBuffer::WriteFailureReportCount()
{
    return TakeFailureReportCount(gWriteFailureCount, gLastWriteFailureReport);
}

unsigned int JackRingBuffer::ReadSpace()
{
    return (jack_ringbuffer_read_space(fRingBuffer) / sizeof(jack_default_audio_sample_t));
}

unsigned int JackRingBuffer::WriteSpace()
{
    return (jack_ringbuffer_write_space(fRingBuffer) / sizeof(jack_default_audio_sample_t));
}

unsigned int JackRingBuffer::Read(jack_default_audio_sample_t* buffer, unsigned int frames)
{
    size_t len = jack_ringbuffer_read_space(fRingBuffer);

    if (len < frames * sizeof(jack_default_audio_sample_t)) {
        uint64_t failure_count = ReadFailureReportCount();
        if (failure_count > 0) {
            jack_error("JackRingBuffer::Read : producer too slow, missing frames = %d; failures since last report = %llu", frames, (unsigned long long)failure_count);
        }
        return 0;
    } else {
        jack_ringbuffer_read(fRingBuffer, (char*)buffer, frames * sizeof(jack_default_audio_sample_t));
        return frames;
    }
}

unsigned int JackRingBuffer::Write(jack_default_audio_sample_t* buffer, unsigned int frames)
{
    size_t len = jack_ringbuffer_write_space(fRingBuffer);

    if (len < frames * sizeof(jack_default_audio_sample_t)) {
        uint64_t failure_count = WriteFailureReportCount();
        if (failure_count > 0) {
            jack_error("JackRingBuffer::Write : consumer too slow, skip frames = %d; failures since last report = %llu", frames, (unsigned long long)failure_count);
        }
        return 0;
    } else {
        jack_ringbuffer_write(fRingBuffer, (char*)buffer, frames * sizeof(jack_default_audio_sample_t));
        return frames;
    }
}

unsigned int JackRingBuffer::Read(void* buffer, unsigned int bytes)
{
    size_t len = jack_ringbuffer_read_space(fRingBuffer);

    if (len < bytes) {
        uint64_t failure_count = ReadFailureReportCount();
        if (failure_count > 0) {
            jack_error("JackRingBuffer::Read : producer too slow, missing bytes = %d; failures since last report = %llu", bytes, (unsigned long long)failure_count);
        }
        return 0;
    } else {
        jack_ringbuffer_read(fRingBuffer, (char*)buffer, bytes);
        return bytes;
    }
}

unsigned int JackRingBuffer::Write(void* buffer, unsigned int bytes)
{
    size_t len = jack_ringbuffer_write_space(fRingBuffer);

    if (len < bytes) {
        uint64_t failure_count = WriteFailureReportCount();
        if (failure_count > 0) {
            jack_error("JackRingBuffer::Write : consumer too slow, skip bytes = %d; failures since last report = %llu", bytes, (unsigned long long)failure_count);
        }
        return 0;
    } else {
        jack_ringbuffer_write(fRingBuffer, (char*)buffer, bytes);
        return bytes;
    }
}

unsigned int JackResampler::ReadResample(jack_default_audio_sample_t* buffer, unsigned int frames)
{
    return Read(buffer, frames);
}

unsigned int JackResampler::WriteResample(jack_default_audio_sample_t* buffer, unsigned int frames)
{
    return Write(buffer, frames);
}

}
