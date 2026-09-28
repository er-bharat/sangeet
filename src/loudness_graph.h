#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace LoudnessGraph {
	
	struct Result {
		double durationSec = 0.0;
		int sampleRate = 0;
		std::vector<double> rms;   // raw linear RMS per bucket, 0..1
		std::vector<double> peak;  // raw linear peak per bucket, 0..1
	};
	
	inline std::string shellEscape(const std::string &path)
	{
		std::string out = "'";
		for (const char c : path) {
			if (c == '\'')
				out += "'\\''";
			else
				out += c;
		}
		return out + "'";
	}
	
	// Decodes any file to mono 44.1 kHz float samples via ffmpeg.
	inline bool decode(const std::string &path, std::vector<float> &samples)
	{
		const std::string cmd =
		"ffmpeg -v error -nostdin -y -i " + shellEscape(path) +
		" -f f32le -acodec pcm_f32le -ac 1 -ar 44100 - < /dev/null";
		
		FILE *pipe = popen(cmd.c_str(), "r");
		if (!pipe)
			return false;
		
		samples.clear();
		float buf[4096];
		size_t n;
		while ((n = fread(buf, sizeof(float), 4096, pipe)) > 0)
			samples.insert(samples.end(), buf, buf + n);
		
		return pclose(pipe) == 0 && !samples.empty();
	}
	
	inline bool analyzeFile(
		const std::string &path,
		Result &result,
		std::string &error,
		int bucketCount = 300)
	{
		if (bucketCount <= 0) {
			error = "Invalid bucket count";
			return false;
		}
		
		std::vector<float> samples;
		if (!decode(path, samples)) {
			error = "ffmpeg could not decode the file";
			return false;
		}
		
		const int sampleRate = 44100;
		const size_t buckets = static_cast<size_t>(bucketCount);
		
		result = Result();
		result.sampleRate = sampleRate;
		result.durationSec = static_cast<double>(samples.size()) / sampleRate;
		result.rms.assign(buckets, 0.0);
		result.peak.assign(buckets, 0.0);
		
		const double bucketSize =
		static_cast<double>(samples.size()) / static_cast<double>(buckets);
		
		for (size_t b = 0; b < buckets; ++b) {
			const size_t begin =
			static_cast<size_t>(static_cast<double>(b) * bucketSize);
			const size_t end = std::min(
				samples.size(),
										static_cast<size_t>(static_cast<double>(b + 1) * bucketSize));
			
			if (begin >= end)
				continue;
			
			double sumSq = 0.0;
			double peak = 0.0;
			
			for (size_t i = begin; i < end; ++i) {
				const double v = std::clamp<double>(samples[i], -1.0, 1.0);
				sumSq += v * v;
				peak = std::max(peak, std::abs(v));
			}
			
			result.rms[b] = std::sqrt(sumSq / static_cast<double>(end - begin));
			result.peak[b] = peak;
		}
		
		return true;
	}
	
}
