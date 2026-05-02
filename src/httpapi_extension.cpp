#define DUCKDB_EXTENSION_MAIN
#define CPPHTTPLIB_OPENSSL_SUPPORT

#include "httpapi_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"

// NOLINTBEGIN
#include <httplib.h>
// NOLINTEND

#include <cctype>
#include <string>
#include <utility>

namespace duckdb {

namespace {

constexpr int kHttpDefaultPort = 80;
constexpr int kHttpsDefaultPort = 443;
constexpr int kDefaultTimeoutSeconds = 30;

enum class HttpMethod { GET, POST, PUT, PATCH, DELETE_, HEAD };

struct ParsedUrl {
	std::string scheme;
	std::string host;
	int port = 0;
	std::string path = "/";
	bool valid = false;
};

ParsedUrl ParseUrl(const std::string &url) {
	ParsedUrl out;
	auto scheme_end = url.find("://");
	if (scheme_end == std::string::npos) {
		return out;
	}
	out.scheme = url.substr(0, scheme_end);
	if (out.scheme != "http" && out.scheme != "https") {
		return out;
	}
	auto host_start = scheme_end + 3;
	auto path_start = url.find('/', host_start);
	std::string host_port;
	if (path_start == std::string::npos) {
		host_port = url.substr(host_start);
		out.path = "/";
	} else {
		host_port = url.substr(host_start, path_start - host_start);
		out.path = url.substr(path_start);
	}
	auto colon = host_port.find(':');
	if (colon == std::string::npos) {
		out.host = host_port;
		out.port = (out.scheme == "https") ? kHttpsDefaultPort : kHttpDefaultPort;
	} else {
		out.host = host_port.substr(0, colon);
		auto port_str = host_port.substr(colon + 1);
		if (port_str.empty()) {
			return out;
		}
		int port = 0;
		for (char c : port_str) {
			if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
				return out;
			}
			port = port * 10 + (c - '0');
			if (port > 65535) {
				return out;
			}
		}
		out.port = port;
	}
	if (out.host.empty()) {
		return out;
	}
	out.valid = true;
	return out;
}

struct HttpResponse {
	int32_t status = 0;
	std::string body;
	std::string error;
};

HttpResponse DoRequest(HttpMethod method, const std::string &url, const std::string &body) {
	HttpResponse resp;
	auto parsed = ParseUrl(url);
	if (!parsed.valid) {
		resp.error = "invalid url: " + url;
		return resp;
	}

	const std::string content_type = "application/json";
	httplib::Client cli(parsed.scheme + "://" + parsed.host + ":" + std::to_string(parsed.port));
	cli.set_follow_location(true);
	cli.set_connection_timeout(kDefaultTimeoutSeconds);
	cli.set_read_timeout(kDefaultTimeoutSeconds);
	cli.set_write_timeout(kDefaultTimeoutSeconds);

	httplib::Result r;
	switch (method) {
	case HttpMethod::GET:
		r = cli.Get(parsed.path);
		break;
	case HttpMethod::POST:
		r = cli.Post(parsed.path, body, content_type);
		break;
	case HttpMethod::PUT:
		r = cli.Put(parsed.path, body, content_type);
		break;
	case HttpMethod::PATCH:
		r = cli.Patch(parsed.path, body, content_type);
		break;
	case HttpMethod::DELETE_:
		r = cli.Delete(parsed.path);
		break;
	case HttpMethod::HEAD:
		r = cli.Head(parsed.path);
		break;
	}
	if (!r) {
		resp.error = httplib::to_string(r.error());
		return resp;
	}
	resp.status = r->status;
	resp.body = r->body;
	return resp;
}

LogicalType HttpResponseType() {
	child_list_t<LogicalType> children;
	children.emplace_back("status", LogicalType::INTEGER);
	children.emplace_back("body", LogicalType::VARCHAR);
	children.emplace_back("error", LogicalType::VARCHAR);
	return LogicalType::STRUCT(std::move(children));
}

void WriteResponse(Vector &result, idx_t row, const HttpResponse &resp) {
	auto &entries = StructVector::GetEntries(result);
	auto &status_vec = *entries[0];
	auto &body_vec = *entries[1];
	auto &error_vec = *entries[2];

	FlatVector::GetData<int32_t>(status_vec)[row] = resp.status;
	FlatVector::GetData<string_t>(body_vec)[row] = StringVector::AddString(body_vec, resp.body);
	if (resp.error.empty()) {
		FlatVector::SetNull(error_vec, row, true);
	} else {
		FlatVector::GetData<string_t>(error_vec)[row] = StringVector::AddString(error_vec, resp.error);
	}
}

template <HttpMethod METHOD>
void HttpNoBodyFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	(void)state;
	auto count = args.size();
	auto &url_vec = args.data[0];
	UnifiedVectorFormat url_format;
	url_vec.ToUnifiedFormat(count, url_format);
	auto urls = UnifiedVectorFormat::GetData<string_t>(url_format);

	for (idx_t i = 0; i < count; i++) {
		auto u_idx = url_format.sel->get_index(i);
		if (!url_format.validity.RowIsValid(u_idx)) {
			FlatVector::SetNull(result, i, true);
			continue;
		}
		auto resp = DoRequest(METHOD, urls[u_idx].GetString(), "");
		WriteResponse(result, i, resp);
	}
}

template <HttpMethod METHOD>
void HttpBodyFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	(void)state;
	auto count = args.size();
	auto &url_vec = args.data[0];
	auto &body_vec = args.data[1];

	UnifiedVectorFormat url_format;
	UnifiedVectorFormat body_format;
	url_vec.ToUnifiedFormat(count, url_format);
	body_vec.ToUnifiedFormat(count, body_format);
	auto urls = UnifiedVectorFormat::GetData<string_t>(url_format);
	auto bodies = UnifiedVectorFormat::GetData<string_t>(body_format);

	for (idx_t i = 0; i < count; i++) {
		auto u_idx = url_format.sel->get_index(i);
		if (!url_format.validity.RowIsValid(u_idx)) {
			FlatVector::SetNull(result, i, true);
			continue;
		}
		auto b_idx = body_format.sel->get_index(i);
		std::string body =
		    body_format.validity.RowIsValid(b_idx) ? bodies[b_idx].GetString() : std::string();
		auto resp = DoRequest(METHOD, urls[u_idx].GetString(), body);
		WriteResponse(result, i, resp);
	}
}

} // namespace

static void LoadInternal(ExtensionLoader &loader) {
	auto resp_type = HttpResponseType();

	loader.RegisterFunction(
	    ScalarFunction("http_get", {LogicalType::VARCHAR}, resp_type, HttpNoBodyFunction<HttpMethod::GET>));
	loader.RegisterFunction(
	    ScalarFunction("http_delete", {LogicalType::VARCHAR}, resp_type, HttpNoBodyFunction<HttpMethod::DELETE_>));
	loader.RegisterFunction(
	    ScalarFunction("http_head", {LogicalType::VARCHAR}, resp_type, HttpNoBodyFunction<HttpMethod::HEAD>));

	loader.RegisterFunction(ScalarFunction("http_post", {LogicalType::VARCHAR, LogicalType::VARCHAR}, resp_type,
	                                       HttpBodyFunction<HttpMethod::POST>));
	loader.RegisterFunction(ScalarFunction("http_put", {LogicalType::VARCHAR, LogicalType::VARCHAR}, resp_type,
	                                       HttpBodyFunction<HttpMethod::PUT>));
	loader.RegisterFunction(ScalarFunction("http_patch", {LogicalType::VARCHAR, LogicalType::VARCHAR}, resp_type,
	                                       HttpBodyFunction<HttpMethod::PATCH>));
}

void HttpapiExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}

std::string HttpapiExtension::Name() {
	return "httpapi";
}

std::string HttpapiExtension::Version() const {
#ifdef EXT_VERSION_HTTPAPI
	return EXT_VERSION_HTTPAPI;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(httpapi, loader) {
	duckdb::LoadInternal(loader);
}
}
