#include "corebpe.hpp"

namespace py = pybind11;

/* CoreBPEģ�鹹�캯�� */
CoreBPE::CoreBPE(std::unordered_map<std::string, Rank> encoder,
	std::unordered_map<std::string, Rank> special_tokens_encoder,
	const std::string& pattern)
{
	this->encoder = encoder;
	this->special_tokens_encoder = special_tokens_encoder;

	// regex: ʶ��һ��tokens��regex
	boost::regex regex;
	boost::regex special_regex;
	try {
		regex = boost::regex(pattern);
	}
	catch (const std::regex_error& e) {
		throw py::value_error(e.what());
	}

	// special_regex: ʶ��special tokens��regex
	std::vector<std::string> special_tokens_encoder_keys;
	boost::copy(special_tokens_encoder | boost::adaptors::map_keys, std::back_inserter(special_tokens_encoder_keys));
	std::transform(special_tokens_encoder_keys.begin(), special_tokens_encoder_keys.end(), special_tokens_encoder_keys.begin(), _escape_regex);
	const std::string separator = "|";
	special_regex = boost::algorithm::join(special_tokens_encoder_keys, separator);

	// decoders��encoders����ӳ��
	for (const auto& [x, y] : encoder) {
		decoder[y] = x;
	}
	if (encoder.size() != decoder.size()) {
		throw py::value_error("Encoder and decoder must be of equal length; maybe you had duplicate token indices in your encoder?");
	}
	for (const auto& [x, y] : special_tokens_encoder) {
		special_tokens_decoder[y] = x;
	}
	
	// ��encoder�е�tokens���ֵ�����
	boost::copy(encoder | boost::adaptors::map_keys, std::back_inserter(sorted_token_bytes));
	std::sort(sorted_token_bytes.begin(), sorted_token_bytes.end());

	// ��regex���Ƶ������̵߳�tls�У���ֹ���̳߳�ͻ
	regex_tls = std::vector(MAX_NUM_THREADS, regex);
	special_regex_tls = std::vector(MAX_NUM_THREADS, special_regex);

}

std::vector<CoreBPE::Rank> CoreBPE::encode(const std::string& text, const std::unordered_set<std::string>& allowed_special)
{
	//py::gil_scoped_release release_gil;
	return _encode_native(text, allowed_special).first;
}

std::vector<CoreBPE::Rank> CoreBPE::byte_pair_encode(const std::string& piece, const std::unordered_map<std::string, Rank>& ranks)
{
	if (piece.empty()) {
		throw py::value_error("Empty string passed to byte_pair_encode");
	}
	std::vector<std::pair<std::size_t, Rank>> merged = _byte_pair_merge(ranks, piece);
	std::vector<Rank> ret;
	for (size_t i = 0; i < merged.size() - 1; i++) {
		auto part_str = piece.substr(merged[i].first, merged[i + 1].first - merged[i].first);
		ret.push_back(ranks.at(part_str));
	}
	return ret;
}

// �Դ�����ַ������б���
std::pair<std::vector<CoreBPE::Rank>, std::size_t> CoreBPE::_encode_native(const std::string& text, const std::unordered_set<std::string>& allowed_special)
{
	boost::regex special_regex = _get_tl_special_regex();
	boost::regex regex = _get_tl_regex();
	std::vector<Rank> ret;

	int start = 0;
	int last_piece_token_len = 0;
	while (true) {
		boost::match_results<std::string::const_iterator> next_special;
		int start_find = start;		// ����ordinary token�����
		bool found_special;

		// Ѱ����һ��allowed_special
		while (true) {
			found_special = boost::regex_search(text.begin() + start_find, text.end(), next_special, special_regex);
			if (found_special) {
				if (allowed_special.count(next_special.str())) {
					break;
				}
				start_find += int(next_special.position()) + 1;		// �����������Ϊ��ǰspecial token����һ���ַ�
			}
			else {
				break;
			}
		}
		int end;					// ����ordinary token���յ�
		// ��һ��ѭ���˳������ֿ����ԣ��ҵ�����һ��allowed_special����û���ҵ�
		if (found_special) {
			end = start_find + int(next_special.position());
		}
		else {
			end = int(text.size());
		}

		// ��������allowed special֮���ordinary tokens
		boost::sregex_iterator it(text.begin() + start, text.begin() + end, regex);
		boost::sregex_iterator it_end;
		for (; it != it_end; it++) {
			// ����regexƥ�䵽��һ��������token
			if (encoder.count(it->str())) {
				last_piece_token_len = 1;
				ret.push_back(encoder.at(it->str()));
			}
			// ƥ�䵽��token����������Ҫ�������ֽڱ���
			else {
				auto tokens = byte_pair_encode(it->str(), encoder);
				last_piece_token_len = int(tokens.size());
				std::copy(tokens.begin(), tokens.end(), std::back_inserter(ret));
			}
		}

		// ���������special tokens�����������������˵���Ѿ������ַ���ĩβ����������
		if (found_special) {
			int token = special_tokens_encoder.at(next_special.str());
			ret.push_back(token);
			start = end + int(next_special.length());
			last_piece_token_len = 0;
		}
		else {
			break;
		}
	}

	return std::make_pair(ret, last_piece_token_len);
}

/* ���ַ���ת��ΪRegex��ʶ����ַ� */
std::string CoreBPE::_escape_regex(const std::string& text)
{
	const boost::regex esc("[.^$|()\\[\\]{}*+?\\\\]");
	const std::string rep("\\\\&");
	std::string result = boost::regex_replace(text, esc, rep, boost::match_default | boost::format_sed);
	return result;
}

boost::regex CoreBPE::_get_tl_regex()
{
	return regex_tls[0];
}

boost::regex CoreBPE::_get_tl_special_regex()
{
	return special_regex_tls[0];
}

std::vector<std::pair<std::size_t, CoreBPE::Rank>> CoreBPE::_byte_pair_merge(const std::unordered_map<std::string, Rank>& ranks, const std::string& piece)
{
	std::vector<std::pair<size_t, Rank>> parts;
	parts.reserve(piece.size() + 1);

	// ��ȡrank��С�ļ�ֵ�ԣ�ĿǰΪO(mn)�ĸ��Ӷȣ����Կ�����priority_queue�Ż���
	std::pair<Rank, size_t> min_rank = { INF, INF };
	for (size_t i = 0; i < piece.size() - 1; i++) {
		auto slice = piece.substr(i, 2);
		Rank rank = INF;
		if (ranks.find(slice) != ranks.end()) {
			rank = ranks.at(slice);
		}
		if (rank < min_rank.first) {
			min_rank = { rank, i };
		}
		parts.push_back({ i,rank });
	}
	parts.push_back({ piece.size() - 1, INF });
	parts.push_back({ piece.size(), INF });

	// ��ȡ�ϲ����rank
	auto get_rank = [&](const std::vector<std::pair<size_t, Rank>>& parts, size_t i) {
		if (i + 3 >= parts.size()) {
			return INF;
		}
		auto slice = piece.substr(parts[i].first, parts[i + 3].first - parts[i].first);
		if (ranks.find(slice) != ranks.end()) {
			return ranks.at(slice);
		}
		else {
			return INF;
		}
	};
	// �ϲ�rank
	while (min_rank.first != INF) {
		size_t i = min_rank.second;
		if (i > 0) {
			parts[i - 1].second = get_rank(parts, i - 1);
		}
		parts[i].second = get_rank(parts, i);
		parts.erase(parts.begin() + i + 1);

		min_rank = { INF, INF };
		for (size_t i = 0; i < parts.size() - 1; i++) {
			if (parts[i].second < min_rank.first) {
				min_rank = { parts[i].second, i };
			}
		}
	}
	return parts;
}
