#pragma once

#include <regex>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/range/adaptor/map.hpp>
#include <boost/range/algorithm/copy.hpp>
#include <boost/regex.hpp>


struct CoreBPE
{
	typedef int Rank;

	static constexpr int INF = INT_MAX;

	CoreBPE(std::unordered_map<std::string, Rank> encoder,
		std::unordered_map<std::string, Rank> special_tokens_encoder,
		const std::string& pattern);
	CoreBPE();

	std::vector<Rank> encode(const std::string& text, const std::unordered_set<std::string>& allowed_special);
	static std::vector<Rank> byte_pair_encode(const std::string& piece, const std::unordered_map<std::string, Rank>& ranks);

	std::pair<std::vector<Rank>, std::size_t> _encode_native(const std::string& text, const std::unordered_set<std::string>& allowed_special);
	static std::string _escape_regex(const std::string &text);
	boost::regex _get_tl_regex();
	boost::regex _get_tl_special_regex();
	static std::vector<std::pair<size_t, Rank>> _byte_pair_merge(const std::unordered_map<std::string, Rank>& ranks, const std::string& piece);
	
	std::unordered_map<std::string, Rank> encoder;
	std::unordered_map<Rank, std::string> decoder;
	std::unordered_map<std::string, Rank> special_tokens_encoder;
	std::unordered_map<Rank, std::string> special_tokens_decoder;

	std::vector<std::string> sorted_token_bytes;

	std::vector<boost::regex> regex_tls;
	std::vector<boost::regex> special_regex_tls;

	static constexpr std::size_t MAX_NUM_THREADS = 128;
};
