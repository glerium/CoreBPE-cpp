#include "corebpe.hpp"

namespace py = pybind11;

PYBIND11_MODULE(CoreBPE_cpp, m) {
	m.doc() = "My CoreBPE Module";
	py::class_<CoreBPE>(m, "CoreBPE")
		// 构造函数
		.def(py::init<std::unordered_map<std::string, int>, std::unordered_map<std::string, int>, std::string>(),
			py::arg("encoder"), py::arg("special_token_encoder"), py::arg("pattern"))
		// 向Python暴露的API函数
		.def("encode", &CoreBPE::encode)
		.def_static("byte_pair_encode", &CoreBPE::byte_pair_encode)
		// 只读属性
		.def_readonly("encoder", &CoreBPE::encoder)
		.def_readonly("decoder", &CoreBPE::decoder)
		.def_readonly("special_tokens_encoder", &CoreBPE::special_tokens_encoder)
		.def_readonly("special_tokens_decoder", &CoreBPE::special_tokens_decoder);
}
