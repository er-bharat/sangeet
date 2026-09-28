#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace LoudnessGraph {
	
	struct WavData {
		uint16_t formatTag = 0;
		uint16_t channels = 0;
		uint32_t sampleRate = 0;
		uint16_t bitsPerSample = 0;
		std::vector<uint8_t> raw;
	};
	
	struct Result {
		double durationSec = 0.0;
		int sampleRate = 0;
		int channels = 1;
		std::vector<double> rms;
		std::vector<double> peak;
	};
	
	inline std::string toLower(std::string s)
	{
		for (auto &c : s)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return s;
	}
	
	inline bool hasExtension(const std::string &path, const std::string &ext)
	{
		if (path.size() < ext.size())
			return false;
		return toLower(path.substr(path.size() - ext.size())) == ext;
	}
	
	inline uint16_t readU16(const uint8_t *p)
	{
		return uint16_t(p[0]) |
		(uint16_t(p[1]) << 8);
	}
	
	inline uint32_t readU32(const uint8_t *p)
	{
		return uint32_t(p[0]) |
		(uint32_t(p[1]) << 8) |
		(uint32_t(p[2]) << 16) |
		(uint32_t(p[3]) << 24);
	}
	
	// Wraps a path in single quotes for safe use in a shell command line,
	// escaping any embedded single quote. Without this, a filename or
	// folder containing an apostrophe (not unusual in music libraries)
	// would either break the command or, worse, let arbitrary shell
	// syntax through.
	inline std::string shellEscape(const std::string &path)
	{
		std::string escaped = "'";
		
		for (const char c : path) {
			if (c == '\'')
				escaped += "'\\''";
			else
				escaped += c;
		}
		
		escaped += "'";
		return escaped;
	}
	
	inline bool loadWav(
		const std::string &path,
		WavData &out,
		std::string &error)
	{
		std::ifstream file(path, std::ios::binary);
		
		if (!file) {
			error = "Could not open WAV file";
			return false;
		}
		
		// Bulk read in one shot. istreambuf_iterator reads one byte at a
		// time and is dramatically slower - especially in a debug/-O0
		// build, where it can dominate total runtime on its own.
		file.seekg(0, std::ios::end);
		const std::streamoff fileSize = file.tellg();
		file.seekg(0, std::ios::beg);
		
		if (fileSize <= 0) {
			error = "Empty or unreadable WAV file";
			return false;
		}
		
		std::vector<uint8_t> data(static_cast<size_t>(fileSize));
		if (!file.read(reinterpret_cast<char*>(data.data()), fileSize)) {
			error = "Failed to read WAV file";
			return false;
		}
		
		if (data.size() < 12 ||
			std::memcmp(data.data(), "RIFF", 4) != 0 ||
			std::memcmp(data.data() + 8, "WAVE", 4) != 0) {
			error = "Not a RIFF/WAVE file";
		return false;
			}
			
			size_t position = 12;
			bool haveFormat = false;
			bool haveData = false;
			
			while (position + 8 <= data.size()) {
				const uint8_t *chunk = data.data() + position;
				const uint32_t chunkSize = readU32(chunk + 4);
				const size_t chunkStart = position + 8;
				
				if (chunkStart > data.size())
					break;
				
				const size_t available = data.size() - chunkStart;
				const size_t actualSize =
				std::min<size_t>(static_cast<size_t>(chunkSize), available);
				
				if (std::memcmp(chunk, "fmt ", 4) == 0) {
					if (actualSize < 16) {
						error = "Invalid fmt chunk";
						return false;
					}
					
					const uint8_t *format = data.data() + chunkStart;
					
					out.formatTag = readU16(format);
					out.channels = readU16(format + 2);
					out.sampleRate = readU32(format + 4);
					out.bitsPerSample = readU16(format + 14);
					
					haveFormat = true;
				}
				else if (std::memcmp(chunk, "data", 4) == 0) {
					const auto begin =
					data.begin() + static_cast<std::ptrdiff_t>(chunkStart);
					
					const auto end =
					data.begin() +
					static_cast<std::ptrdiff_t>(chunkStart + actualSize);
					
					out.raw.assign(begin, end);
					haveData = true;
				}
				
				position = chunkStart + actualSize + (actualSize & size_t(1));
			}
			
			if (!haveFormat) {
				error = "No fmt chunk";
				return false;
			}
			
			if (!haveData) {
				error = "No audio data";
				return false;
			}
			
			if (out.channels == 0) {
				error = "Invalid channel count";
				return false;
			}
			
			if (out.sampleRate == 0) {
				error = "Invalid sample rate";
				return false;
			}
			
			if (out.bitsPerSample == 0) {
				error = "Invalid bit depth";
				return false;
			}
			
			if (out.formatTag != 1 && out.formatTag != 3) {
				error = "Unsupported WAV format";
				return false;
			}
			
			return true;
	}
	
	inline double decodeSample(
		const uint8_t *data,
		uint16_t format,
		uint16_t bits)
	{
		if (format == 3) {
			if (bits == 32) {
				float value;
				std::memcpy(&value, data, sizeof(value));
				return std::clamp<double>(value, -1.0, 1.0);
			}
			
			if (bits == 64) {
				double value;
				std::memcpy(&value, data, sizeof(value));
				return std::clamp(value, -1.0, 1.0);
			}
			
			return 0.0;
		}
		
		switch (bits) {
			case 8:
				return (static_cast<int>(*data) - 128) / 128.0;
				
			case 16: {
				const int16_t value =
				static_cast<int16_t>(
					uint16_t(data[0]) |
					(uint16_t(data[1]) << 8));
				
				return static_cast<double>(value) / 32768.0;
			}
			
			case 24: {
				int32_t value =
				int32_t(data[0]) |
				(int32_t(data[1]) << 8) |
				(int32_t(data[2]) << 16);
				
				if (value & 0x800000)
					value |= ~0xFFFFFF;
				
				return static_cast<double>(value) / 8388608.0;
			}
			
			case 32: {
				const int32_t value =
				static_cast<int32_t>(
					uint32_t(data[0]) |
					(uint32_t(data[1]) << 8) |
					(uint32_t(data[2]) << 16) |
					(uint32_t(data[3]) << 24));
				
				return static_cast<double>(value) / 2147483648.0;
			}
			
			default:
				return 0.0;
		}
	}
	
	inline bool decodeWav(
		const WavData &wav,
		std::vector<float> &samples,
		std::string &error)
	{
		const size_t bytesPerSample =
		static_cast<size_t>(wav.bitsPerSample) / 8;
		
		if (bytesPerSample == 0) {
			error = "Invalid sample size";
			return false;
		}
		
		const size_t frameSize =
		bytesPerSample * static_cast<size_t>(wav.channels);
		
		if (frameSize == 0) {
			error = "Invalid frame size";
			return false;
		}
		
		const size_t frames = wav.raw.size() / frameSize;
		
		samples.clear();
		samples.reserve(frames);
		
		for (size_t frame = 0; frame < frames; ++frame) {
			double sum = 0.0;
			
			for (uint16_t channel = 0;
				 channel < wav.channels;
			++channel) {
				const size_t offset =
				frame * frameSize +
				static_cast<size_t>(channel) * bytesPerSample;
				
				sum += decodeSample(
					wav.raw.data() + offset,
									wav.formatTag,
						wav.bitsPerSample);
			}
			
			samples.push_back(
				static_cast<float>(
					sum / static_cast<double>(wav.channels)));
		}
		
		return true;
	}
	
	inline bool decodeWithFfmpeg(
		const std::string &path,
		std::vector<float> &samples,
		int &sampleRate,
		std::string &error)
	{
		// -nostdin is the important part: without it, ffmpeg puts the
		// inherited terminal into raw mode to listen for keypresses
		// (q to quit, etc). If this process gets force-closed while
		// ffmpeg is still running -- e.g. it's stuck decoding an
		// unusual high-sample-rate file -- ffmpeg never gets to restore
		// the terminal, leaving it in raw mode afterwards. Redirecting
		// stdin from /dev/null is a second layer of the same guard, and
		// -y avoids any chance of an overwrite prompt.
		const std::string command =
		std::string("ffmpeg -v error -nostdin -y -i ") +
		shellEscape(path) +
		" -f f32le -acodec pcm_f32le -ac 1 -ar 44100 - < /dev/null";
		
		FILE *pipe = popen(command.c_str(), "r");
		
		if (!pipe) {
			error = "Could not start ffmpeg";
			return false;
		}
		
		samples.clear();
		
		float buffer[4096];
		
		while (true) {
			const size_t count =
			fread(buffer, sizeof(float), 4096, pipe);
			
			if (count == 0)
				break;
			
			samples.insert(
				samples.end(),
						   buffer,
				  buffer + count);
		}
		
		const int status = pclose(pipe);
		
		if (status != 0 || samples.empty()) {
			error = "ffmpeg could not decode the file";
			samples.clear();
			return false;
		}
		
		sampleRate = 44100;
		return true;
	}
	
	// --- Dramatization tuning -------------------------------------------
	// These two constants shape how the raw dB (log) data gets turned into
	// the normalized [0, 1] values the graph actually draws.
	//
	// floorDb: how far below 0 dBFS counts as "silence". Widening this
	// (more negative) gives quiet passages more room to fall, so soft
	// sections read as visibly quieter instead of hugging a flat baseline.
	//
	// dramaGamma: exponent applied to the normalized value after the dB
	// conversion. Since the input is already log-scaled, this reshapes the
	// *curve* of that log data rather than re-deriving it:
	//   > 1.0  -> pushes mid/low levels down, exaggerating peaks (punchier)
	//   = 1.0  -> unchanged, linear-in-dB mapping (original behavior)
	//   < 1.0  -> lifts quiet detail, flattens dynamics (gentler)
	constexpr double floorDb = -60.0;
	constexpr double dramaGamma = 1.6;
	
	// After normalization, the whole track is rescaled so its *average*
	// peak bucket lands at targetAvgPeak instead of wherever the fixed dB
	// floor happens to put it. Quiet-mastered tracks that used to sit low
	// on the graph now stretch up to use the full height, and the loudest
	// moments push past the average and can hit the top (clamped at 1.0).
	constexpr double targetAvgPeak = 0.95;
	
	inline double dramatize(double normalized)
	{
		return std::pow(std::clamp(normalized, 0.0, 1.0), dramaGamma);
	}
	
	inline Result calculate(
		const std::vector<float> &samples,
		int sampleRate,
		int bucketCount = 300)
	{
		Result result;
		
		result.sampleRate = sampleRate;
		result.channels = 1;
		
		if (sampleRate <= 0 ||
			samples.empty() ||
			bucketCount <= 0) {
			return result;
			}
			
			result.durationSec =
			static_cast<double>(samples.size()) /
			static_cast<double>(sampleRate);
		
		const size_t bucketCountSize =
		static_cast<size_t>(bucketCount);
		
		result.rms.resize(bucketCountSize);
		result.peak.resize(bucketCountSize);
		
		const double bucketSize =
		static_cast<double>(samples.size()) /
		static_cast<double>(bucketCount);
		
		for (int bucket = 0;
			 bucket < bucketCount;
		++bucket) {
			const size_t bucketIndex =
			static_cast<size_t>(bucket);
			
			const size_t begin =
			static_cast<size_t>(
				static_cast<double>(bucket) * bucketSize);
			
			const size_t end =
			std::min(
				samples.size(),
					 static_cast<size_t>(
						 static_cast<double>(bucket + 1) *
						 bucketSize));
			
			if (begin >= end)
				continue;
			
			double sumSquares = 0.0;
			double peak = 0.0;
			
			for (size_t i = begin; i < end; ++i) {
				const double value =
				std::clamp<double>(
					static_cast<double>(samples[i]),
								   -1.0,
					   1.0);
				
				sumSquares += value * value;
				peak = std::max(peak, std::abs(value));
			}
			
			const double count =
			static_cast<double>(end - begin);
			
			const double rms =
			std::sqrt(sumSquares / count);
			
			const double rmsDb =
			rms > 0.0
			? 20.0 * std::log10(rms)
			: floorDb;
			
			const double peakDb =
			peak > 0.0
			? 20.0 * std::log10(peak)
			: floorDb;
			
			const double normalizedRms =
			std::clamp(
				(rmsDb - floorDb) / -floorDb,
					   0.0,
			  1.0);
			
			const double normalizedPeak =
			std::clamp(
				(peakDb - floorDb) / -floorDb,
					   0.0,
			  1.0);
			
			result.rms[bucketIndex] = dramatize(normalizedRms);
			result.peak[bucketIndex] = dramatize(normalizedPeak);
		}
		
		// Auto-gain pass: scale everything so the track's own average
		// peak reaches targetAvgPeak, rather than being stuck wherever
		// the fixed dB floor placed it.
		double peakSum = 0.0;
		for (const double value : result.peak)
			peakSum += value;
		
		const double avgPeak =
		result.peak.empty()
		? 0.0
		: peakSum / static_cast<double>(result.peak.size());
		
		if (avgPeak > 1e-6) {
			const double gain = targetAvgPeak / avgPeak;
			
			for (double &value : result.rms)
				value = std::clamp(value * gain, 0.0, 1.0);
			
			for (double &value : result.peak)
				value = std::clamp(value * gain, 0.0, 1.0);
		}
		
		return result;
	}
	
	inline bool analyzeFile(
		const std::string &path,
		Result &result,
		std::string &error,
		int bucketCount = 300)
	{
		// Only attempt the native WAV parser for .wav files. Calling it
		// unconditionally on every file (as before) meant every mp3/flac
		// paid the cost of a full-file read just to be told "not a WAV"
		// and have that buffer thrown away - by far the biggest single
		// waste in this code, and especially punishing in a debug/-O0
		// build where that read isn't optimized away.
		if (hasExtension(path, ".wav")) {
			WavData wav;
			if (loadWav(path, wav, error)) {
				std::vector<float> samples;
				
				if (!decodeWav(wav, samples, error))
					return false;
				
				result = calculate(
					samples,
					static_cast<int>(wav.sampleRate),
								   bucketCount);
				
				return !result.rms.empty();
			}
			// Fall through to ffmpeg in case it's a WAV variant our tiny
			// parser doesn't understand (WAVE_FORMAT_EXTENSIBLE, ADPCM...).
		}
		
		std::vector<float> samples;
		int sampleRate = 0;
		
		if (!decodeWithFfmpeg(
			path,
			samples,
			sampleRate,
			error)) {
			return false;
			}
			
			result = calculate(
				samples,
				sampleRate,
				bucketCount);
			
			return !result.rms.empty();
	}
	
}
