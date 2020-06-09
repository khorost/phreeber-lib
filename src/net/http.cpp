﻿#ifndef NOMINMAX
 #define NOMINMAX
#endif

#include <string>
#include <algorithm>
#include <time.h>

#include "net/http.h"
#include "system/fastfile.h"
#include "util/utils.h"
#include "app/khl-define.h"

#ifdef WIN32
#include "shlwapi.h"
#include "win/strptime.h"
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#define MAX_PATH PATH_MAX
#endif

#ifdef _MSC_VER
#define atoll _atoi64
#endif

#pragma comment(lib,"shlwapi.lib")
#pragma comment(lib,"libcurl.lib")

using namespace khorost;
using namespace khorost::network;

bool find_sub_value(const char* pSource_, size_t nSourceSize_, const char* pMatch_, size_t nMatchSize_, char cDivKV,
                    char cDivKK, const char** pResult_ = nullptr, size_t* pnResultSize_ = nullptr) {
    if (pSource_ == nullptr) {
        return false;
    }

    if (nSourceSize_ == data::auto_buffer_char::npos) {
        nSourceSize_ = strlen(pSource_);
    }

    if (nSourceSize_ < nMatchSize_) {
        return false;
    }

    auto pMax = nSourceSize_ - nMatchSize_;
    bool bInQuote = false;
    for (size_t pStart = 0, pCheck = 0; pCheck <= pMax; ++pCheck) {
        if (pSource_[pCheck] == '\"') {
            bInQuote = !bInQuote;
        } else if (!bInQuote && memcmp(pSource_ + pCheck, pMatch_, nMatchSize_) == 0) {
            if (pResult_ != nullptr && pnResultSize_ != nullptr) {
                *pResult_ = nullptr;
                *pnResultSize_ = 0;

                for (pCheck += nMatchSize_, pMax = nSourceSize_; pCheck < pMax; ++pCheck) {
                    if (pSource_[pCheck] == '\"') {
                        bInQuote = !bInQuote;
                    } else if (!bInQuote) {
                        if (pSource_[pCheck] == cDivKV) {
                            pStart = pCheck + sizeof(char);
                            *pResult_ = pSource_ + pStart;
                        } else if (pSource_[pCheck] == cDivKK) {
                            --pCheck;
                            break;
                        }
                    }
                }

                if (*pResult_ != nullptr) {
                    if (pCheck == pMax) {
                        --pCheck;
                    }
                    // значение имеется, его нужно проверить
                    for (; *(*pResult_) == ' '; ++(*pResult_), ++pStart) {
                    }
                    if (*(*pResult_) == '\"') {
                        ++(*pResult_);
                        ++pStart;
                    }
                    for (; *(pSource_ + pCheck) == ' '; --pCheck) {
                    }
                    if (*(pSource_ + pCheck) == '\"') {
                        --pCheck;
                    }
                    *pnResultSize_ = pCheck - pStart + 1;
                }
            }

            return true;
        }
    }

    return false;
}

void http_text_protocol_header::response::set_header_param(const std::string& key, const std::string& value) {
    m_header_params_.insert(std::pair<std::string, std::string>(key, value));
}

void http_text_protocol_header::response::send_header_data(connection& connect) {
    for (const auto& it : m_header_params_) {
        connect.send_string(it.first);
        connect.send_string(": ");
        connect.send_string(it.second);
        connect.send_string(HTTP_ATTRIBUTE_ENDL, sizeof(HTTP_ATTRIBUTE_ENDL) - 1);
    }
}

bool http_text_protocol_header::get_chunk(const char*& rpBuffer_, size_t& rnBufferSize_, char cPrefix_,
                                          const char* pDiv_, data::auto_buffer_char& abTarget_,
                                          data::auto_buffer_chunk_char& rabcQueryValue_, size_t& rnChunkSize_) {
    size_t s;
    // зачишаем префикс от white символов
    for (s = 0; s < rnBufferSize_ && rpBuffer_[s] == cPrefix_; ++s) {
    }

    for (size_t k = s; k < rnBufferSize_; ++k) {
        // выделяем chunk до stop символов
        for (size_t t = 0; pDiv_[t] != '\0'; ++t) {
            if (rpBuffer_[k] == pDiv_[t]) {
                // stop символ найден, chunk полный
                t = abTarget_.get_fill_size();
                rnChunkSize_ = k + sizeof(char);
                abTarget_.append(rpBuffer_, rnChunkSize_);
                abTarget_[t + k] = '\0'; // заменяем stop символ на завершение zстроки
                rabcQueryValue_.set_reference(t + s);

                rpBuffer_ += rnChunkSize_;
                rnBufferSize_ -= rnChunkSize_;

                return true;
            }
        }
    }
    return false;
}

size_t http_text_protocol_header::process_data(const boost::uint8_t* buffer, size_t buffer_size) {
    size_t nProcessByte = 0, nChunkSize, nChunkSizeV;
    switch (m_eHeaderProcess) {
    case eProcessingFirst:
        //  GET / POST ****************************************************
        if (!m_query_method_.is_valid()) {
            if (!get_chunk(reinterpret_cast<const char*&>(buffer), buffer_size, ' ', " ", m_abHeader, m_query_method_,
                           nChunkSize)) {
                return nProcessByte;
            } else {
                nProcessByte += nChunkSize;
            }
        }
        //  /index.html ***************************************************
        if (!m_query_uri_.is_valid()) {
            if (!get_chunk(reinterpret_cast<const char*&>(buffer), buffer_size, ' ', "? ", m_abHeader, m_query_uri_,
                           nChunkSize)) {
                return nProcessByte;
            } else {
                if (*(buffer - sizeof(char)) == '?') {
                    --nChunkSize;
                    buffer -= sizeof(char);
                    buffer_size += sizeof(char);
                }
                nProcessByte += nChunkSize;
            }
        }
        //  ?key=val& ***************************************************
        if (m_abParams.get_fill_size() == 0 && buffer[0] == '?') {
            data::auto_buffer_chunk_char abcParam(m_abParams);
            buffer += sizeof(char);
            buffer_size -= sizeof(char);
            if (!get_chunk(reinterpret_cast<const char*&>(buffer), buffer_size, '?', " ", m_abParams, abcParam,
                           nChunkSize)) {
                return nProcessByte;
            } else {
                nProcessByte += nChunkSize + sizeof(char);
            }
        }
        //  HTTP/1.1   ****************************************************
        if (!m_query_version_.is_valid()) {
            if (!get_chunk(reinterpret_cast<const char*&>(buffer), buffer_size, ' ', "\r\n", m_abHeader,
                           m_query_version_, nChunkSize)) {
                return nProcessByte;
            } else {
                if (0 < buffer_size && (buffer[0] == '\r' || buffer[0] == '\n')) {
                    ++nChunkSize;
                    buffer += sizeof(char);
                    buffer_size -= sizeof(char);
                }
                nProcessByte += nChunkSize;

                m_eHeaderProcess = eProcessingNext;
            }
        }
    case eProcessingNext:
        while (buffer_size >= 2 * sizeof(char)) {
            if (buffer[0] == '\r' && buffer[1] == '\n') {
                m_eHeaderProcess = eSuccessful;
                m_eBodyProcess = eProcessingFirst;

                buffer += 2 * sizeof(char);
                buffer_size -= 2 * sizeof(char);
                nProcessByte += 2 * sizeof(char);
                break;
            }

            data::auto_buffer_chunk_char abcHeaderKey(m_abHeader);
            data::auto_buffer_chunk_char abcHeaderValue(m_abHeader);

            if (get_chunk(reinterpret_cast<const char*&>(buffer), buffer_size, ' ', ":", m_abHeader, abcHeaderKey,
                          nChunkSize)
                && get_chunk(reinterpret_cast<const char*&>(buffer), buffer_size, ' ', "\r\n", m_abHeader,
                             abcHeaderValue, nChunkSizeV)) {
                if (0 < buffer_size && (buffer[0] == '\r' || buffer[0] == '\n')) {
                    ++nChunkSize;
                    buffer += sizeof(char);
                    buffer_size -= sizeof(char);
                }
                nProcessByte += nChunkSize + nChunkSizeV;

                m_header_values.push_back(std::make_pair(abcHeaderKey.get_reference(), abcHeaderValue.get_reference()));
                if (m_request_.m_content_length_ == -1 && strcmp(
                    HTTP_ATTRIBUTE_CONTENT_LENGTH, abcHeaderKey.get_chunk()) == 0) {
                    m_request_.m_content_length_ = atoi(abcHeaderValue.get_chunk());
                } else if (strcmp(HTTP_ATTRIBUTE_COOKIE, abcHeaderKey.get_chunk()) == 0) {
                    parse_string(const_cast<char*>(abcHeaderValue.get_chunk()), nChunkSizeV,
                                 abcHeaderValue.get_reference(), m_cookies_, ';', true);
                }
            } else {
                m_abHeader.decrement_free_size(nChunkSize);
                break;
            }
        }
        break;
    }

    if (m_eBodyProcess == eProcessingFirst || m_eBodyProcess == eProcessingNext) {
        nChunkSize = std::min(buffer_size, m_request_.m_content_length_);
        if (m_request_.m_content_length_ == std::string::npos || (buffer_size == 0 && m_request_.m_content_length_ == 0)
        ) {
            m_eBodyProcess = eSuccessful;
            m_request_.m_content_length_ = 0;
        } else if (nChunkSize != 0) {
            m_abBody.append(reinterpret_cast<const char*>(buffer), nChunkSize);
            nProcessByte += nChunkSize;

            if (m_abBody.get_fill_size() >= m_request_.m_content_length_) {
                m_eBodyProcess = eSuccessful;

                const char* content_type = get_header_parameter(HTTP_ATTRIBUTE_CONTENT_TYPE, nullptr);
                if (content_type != nullptr && find_sub_value(content_type, data::auto_buffer_char::npos,
                                                              HTTP_ATTRIBUTE_CONTENT_TYPE__FORM,
                                                              sizeof(HTTP_ATTRIBUTE_CONTENT_TYPE__FORM) - 1, '=',
                                                              ';')) {
                    //                if (strcmp(HTTP_ATTRIBUTE_CONTENT_TYPE__FORM, pszContentType) == 0) {
                    size_t k = m_abParams.get_fill_size();
                    if (k != 0) {
                        if (m_abParams.get_element(k - 1) == '\0') {
                            if (k > 1 && m_abParams.get_element(k - 2) != '&') {
                                m_abParams[k - 1] = '&';
                            } else {
                                m_abParams.increment_free_size(sizeof(char));
                            }
                        } else if (m_abParams.get_element(k - 1) != '&') {
                            m_abParams.append("&", sizeof(char));
                        }
                        m_request_.m_content_length_ += static_cast<int>(m_abParams.get_fill_size());
                    }

                    m_abParams.append(m_abBody.get_head(), m_abBody.get_fill_size());
                    m_abParams.append("\0", sizeof(char));
                }
            }
        }
        if (m_eBodyProcess == eSuccessful) {
            parse_string(m_abParams.get_position(0), m_abParams.get_fill_size(), 0, m_params_value_, '&', false);
        }
    }

    return nProcessByte;
}

bool http_text_protocol_header::get_multi_part(size_t& current_iterator, std::string& part_name,
                                               std::string& part_content_type, const char*& buffer_content,
                                               size_t& buffer_content_size) {
    const auto header_content_type = get_header_parameter(HTTP_ATTRIBUTE_CONTENT_TYPE, nullptr);
    if (header_content_type == nullptr) {
        return false;
    }

    const char* pszBoundary = nullptr;
    const auto header_content_type_length = strlen(header_content_type);
    size_t szBoundary = 0;

    if (find_sub_value(header_content_type, header_content_type_length,
                       HTTP_ATTRIBUTE_CONTENT_TYPE__MULTIPART_FORM_DATA,
                       sizeof(HTTP_ATTRIBUTE_CONTENT_TYPE__MULTIPART_FORM_DATA) - 1, '=', ';')) {
        if (find_sub_value(header_content_type, header_content_type_length, HTTP_ATTRIBUTE_CONTENT_TYPE__BOUNDARY,
                           sizeof(HTTP_ATTRIBUTE_CONTENT_TYPE__BOUNDARY) - 1, '=', ';', &pszBoundary, &szBoundary)) {
            std::string boundary_label = HTTP_ATTRIBUTE_LABEL_CD;
            boundary_label.append(pszBoundary, szBoundary);
            // проверить что контрольная метка правильная
            if (m_abBody.compare(current_iterator, boundary_label.c_str(), boundary_label.size()) != 0) {
                return false;
            }
            current_iterator += boundary_label.size();

            if (m_abBody.compare(current_iterator, HTTP_ATTRIBUTE_LABEL_CD, sizeof(HTTP_ATTRIBUTE_LABEL_CD) - 1) == 0) {
                return false; // завершение блока
            }

            if (m_abBody.compare(current_iterator, HTTP_ATTRIBUTE_ENDL, sizeof(HTTP_ATTRIBUTE_ENDL) - 1) != 0) {
                return false;
            }
            current_iterator += sizeof(HTTP_ATTRIBUTE_ENDL) - 1;

            const auto p_max = m_abBody.get_fill_size();
            while (current_iterator < p_max) {
                auto p_end_line = m_abBody.find(current_iterator, HTTP_ATTRIBUTE_ENDL, sizeof(HTTP_ATTRIBUTE_ENDL) - 1);
                if (p_end_line == data::auto_buffer_char::npos) {
                    p_end_line = m_abBody.get_fill_size() - current_iterator;
                }

                if (p_end_line == current_iterator) {
                    current_iterator += sizeof(HTTP_ATTRIBUTE_ENDL) - 1;
                    boundary_label.insert(0, HTTP_ATTRIBUTE_ENDL);
                    p_end_line = m_abBody.find(current_iterator, boundary_label.c_str(), boundary_label.size());
                    if (p_end_line == data::auto_buffer_char::npos) {
                        return false;
                    }

                    buffer_content = m_abBody.get_head() + current_iterator;
                    buffer_content_size = p_end_line - current_iterator;
                    current_iterator = p_end_line + sizeof(HTTP_ATTRIBUTE_ENDL) - 1;

                    return true;
                }

                const char* value;
                size_t value_size;
                if (m_abBody.compare(current_iterator, HTTP_ATTRIBUTE_CONTENT_DISPOSITION,
                                     sizeof(HTTP_ATTRIBUTE_CONTENT_DISPOSITION) - 1) == 0) {
                    if (find_sub_value(m_abBody.get_head() + current_iterator, p_end_line - current_iterator, "name",
                                       sizeof("name") - 1, '=', ';', &value, &value_size)) {
                        part_name.assign(value, value_size);
                    }
                } else if (m_abBody.compare(current_iterator, HTTP_ATTRIBUTE_CONTENT_TYPE,
                                            sizeof(HTTP_ATTRIBUTE_CONTENT_TYPE) - 1) == 0) {
                    if (find_sub_value(m_abBody.get_head() + current_iterator, p_end_line - current_iterator,
                                       HTTP_ATTRIBUTE_CONTENT_TYPE, sizeof(HTTP_ATTRIBUTE_CONTENT_TYPE) - 1, ':', ';',
                                       &value, &value_size)) {
                        part_content_type.assign(value, value_size);
                    }
                }
                current_iterator = p_end_line + sizeof(HTTP_ATTRIBUTE_ENDL) - 1;
            }
        }
    }

    // Content-Type: multipart/form-data; boundary=xmyidlatfdmcrqnk; charset=UTF-8
    // Content-Disposition: form-data; name="image"; filename="1.png"
    //    Content - Type: image / png


    return false;
}

bool http_text_protocol_header::parse_string(char* pBuffer_, size_t nBufferSize_, size_t nShift, ListPairs& lpTarget_,
                                             char cDiv, bool bTrim) {
    size_t k, v, t;
    for (k = 0, v = -1, t = 0; t < nBufferSize_;) {
        if (t >= nBufferSize_ || pBuffer_[t] == '\0') {
            if (bTrim) {
                for (; pBuffer_[k] != '\0' && pBuffer_[k] == ' '; ++k) {
                }
            }
            lpTarget_.push_back(std::make_pair(nShift + k, v != -1 ? nShift + v : v));
            break;
        } else if (pBuffer_[t] == cDiv) {
            pBuffer_[t] = '\0';
            if (bTrim) {
                for (; pBuffer_[k] != '\0' && pBuffer_[k] == ' '; ++k) {
                }
            }
            lpTarget_.push_back(std::make_pair(nShift + k, v != -1 ? nShift + v : v));
            k = ++t;
            v = -1;
        } else if (pBuffer_[t] == '=') {
            pBuffer_[t] = '\0';
            v = ++t;
        } else {
            ++t;
        }
    }
    return true;
}

const char* http_text_protocol_header::get_cookie_parameter(const std::string& key, const char* default_value) const {
    auto value = get_cookie(key, nullptr);
    if (value == nullptr) {
        value = get_parameter(key, default_value);
    }
    return value;
}

const char* http_text_protocol_header::get_cookie(const std::string& key, const char* default_value) const {
    for (const auto& cit : m_cookies_) {
        if (strcmp(key.c_str(), m_abHeader.get_position(cit.first)) == 0) {
            return cit.second != std::string::npos ? m_abHeader.get_position(cit.second) : default_value;
        }
    }
    return default_value;
}

bool http_text_protocol_header::is_parameter_exist(const std::string& key) const {
    for (const auto& cit : m_params_value_) {
        if (strcmp(key.c_str(), m_abParams.get_position(cit.first)) == 0) {
            return true;
        }
    }
    return false;
}

size_t http_text_protocol_header::get_parameter_index(const std::string& key) const {
    for (const auto& cit : m_params_value_) {
        if (strcmp(key.c_str(), m_abParams.get_position(cit.first)) == 0) {
            return cit.second;
        }
    }
    return -1;
}

size_t http_text_protocol_header::get_header_index(const std::string& key) const {
    for (const auto& cit : m_header_values) {
        if (strcmp(key.c_str(), m_abHeader.get_position(cit.first)) == 0) {
            return cit.second;
        }
    }
    return -1;
}

void http_text_protocol_header::FillParameter2Array(const std::string& sKey_, std::vector<int>& rArray_) {
    std::string sKey2 = sKey_ + "[]";
    for (const auto& p : m_params_value_) {
        if (strcmp(sKey_.c_str(), m_abParams.get_position(p.first)) == 0 || strcmp(
            sKey2.c_str(), m_abParams.get_position(p.first)) == 0) {
            rArray_.push_back(atoi(m_abParams.get_position(p.second)));
        }
    }
}

const char* http_text_protocol_header::get_parameter(const std::string& key, const char* default_value) const {
    for (const auto& cit : m_params_value_) {
        if (strcmp(key.c_str(), m_abParams.get_position(cit.first)) == 0) {
            return cit.second != std::string::npos ? m_abParams.get_position(cit.second) : default_value;
        }
    }
    return default_value;
}

const char* http_text_protocol_header::get_header_parameter(const std::string& param, const char* default_value) const {
    for (const auto& cit : m_header_values) {
        if (strcmp(param.c_str(), m_abHeader.get_position(cit.first)) == 0) {
            return m_abHeader.get_position(cit.second);
        }
    }

    return default_value;
}

bool http_text_protocol_header::is_send_data() const {
    return strcmp(get_query_method(), "HEAD") != 0;
}

void http_text_protocol_header::send_response(network::connection& connect, const char* response, const size_t length) {
    using namespace boost::posix_time;

    char st[255];

    m_response_.m_content_length_ = length;

    const auto http_version = m_query_version_.get_chunk();

    connect.send_string(http_version);
    connect.send_string(" ", sizeof(char));
    connect.send_number(m_response_.m_nCode);
    connect.send_string(" ", sizeof(char));
    connect.send_string(m_response_.m_sCodeReason);
    connect.send_string(HTTP_ATTRIBUTE_ENDL, sizeof(HTTP_ATTRIBUTE_ENDL) - 1);
    connect.send_string("Server: phreeber" HTTP_ATTRIBUTE_ENDL);
    time_t n = time(nullptr);
    strftime(st, sizeof(st), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&n));
    connect.send_string("Date: ");
    connect.send_string(st);
    connect.send_string(HTTP_ATTRIBUTE_ENDL, sizeof(HTTP_ATTRIBUTE_ENDL) - 1);

    if (!m_response_.m_sRedirectURL.empty()) {
        connect.send_string("Location: ");
        connect.send_string(m_response_.m_sRedirectURL);
        connect.send_string(HTTP_ATTRIBUTE_ENDL, sizeof(HTTP_ATTRIBUTE_ENDL) - 1);
    }

    const auto connection_keep_alive = get_header_parameter(
        HTTP_ATTRIBUTE_CONNECTION, strcmp(http_version, HTTP_VERSION_1_1) == 0
                                       ? HTTP_ATTRIBUTE_CONNECTION__KEEP_ALIVE
                                       : HTTP_ATTRIBUTE_CONNECTION__CLOSE);
    m_response_.m_auto_close = strcmp(HTTP_ATTRIBUTE_CONNECTION__KEEP_ALIVE, connection_keep_alive) != 0;

    connect.send_string(
        std::string("Connection: ") + std::string(connection_keep_alive) + std::string(HTTP_ATTRIBUTE_ENDL));

    // CORS
    /*    const auto origin = get_header_parameter(HTTP_ATTRIBUTE__ORIGIN, nullptr);
        if (origin != nullptr) {
            connect.send_string(HTTP_ATTRIBUTE__ACCESS_CONTROL_ALLOW_ORIGIN ": " + std::string(origin) + std::string(HTTP_ATTRIBUTE_ENDL));
            connect.send_string(HTTP_ATTRIBUTE__ACCESS_CONTROL_ALLOW_CREDENTIALS ": true" HTTP_ATTRIBUTE_ENDL);
        }
    */
    for (const auto& c : m_response_.m_cookies_) {
        time_t gmt = data::epoch_diff(c.m_dtExpire).total_seconds();
        strftime(st, sizeof(st), "%a, %d-%b-%Y %H:%M:%S GMT", gmtime(&gmt));

        connect.send_string(
            std::string("Set-Cookie: ") + c.m_sCookie + std::string("=") + c.m_sValue + std::string("; Expires=") +
            std::string(st) + std::string("; Domain=.") + c.m_sDomain + std::string("; Path=/") +
            (c.m_http_only ? std::string("; HttpOnly ") : "") + HTTP_ATTRIBUTE_ENDL);
    }

    connect.send_string(std::string(HTTP_ATTRIBUTE_CONTENT_TYPE HTTP_ATTRIBUTE_DIV) + m_response_.m_sContentType);
    if (!m_response_.m_sContentTypeCP.empty()) {
        connect.send_string(std::string("; charset=") + m_response_.m_sContentTypeCP);
    }
    connect.send_string(HTTP_ATTRIBUTE_ENDL, sizeof(HTTP_ATTRIBUTE_ENDL) - 1);

    if (!m_response_.m_sContentDisposition.empty()) {
        connect.send_string(
            std::string(HTTP_ATTRIBUTE_CONTENT_DISPOSITION HTTP_ATTRIBUTE_DIV) + m_response_.m_sContentDisposition);
        connect.send_string(HTTP_ATTRIBUTE_ENDL, sizeof(HTTP_ATTRIBUTE_ENDL) - 1);
    }

    connect.send_string(HTTP_ATTRIBUTE_CONTENT_LENGTH ": ");
    connect.send_number(static_cast<unsigned int>(m_response_.m_content_length_));
    connect.send_string(HTTP_ATTRIBUTE_ENDL, sizeof(HTTP_ATTRIBUTE_ENDL) - 1);

    if (!m_response_.m_tLastModify.is_not_a_date_time()) {
        connect.send_string(HTTP_ATTRIBUTE_LAST_MODIFIED HTTP_ATTRIBUTE_DIV);
        time_t gmt = data::epoch_diff(m_response_.m_tLastModify).total_seconds();
        strftime(st, sizeof(st), "%a, %d-%b-%Y %H:%M:%S GMT", gmtime(&gmt));
        connect.send_string(st);
        connect.send_string(HTTP_ATTRIBUTE_ENDL, sizeof(HTTP_ATTRIBUTE_ENDL) - 1);
    }

    m_response_.send_header_data(connect);

    connect.send_string(HTTP_ATTRIBUTE_ENDL, sizeof(HTTP_ATTRIBUTE_ENDL) - 1);
    if (is_send_data() && response != nullptr) {
        connect.send_string(response, m_response_.m_content_length_);
    }
}

void http_text_protocol_header::set_cookie(const std::string& cookie, const std::string& value,
                                           boost::posix_time::ptime expire, const std::string& domain, bool http_only) {
    m_response_.m_cookies_.emplace_back(cookie, value, expire, domain, http_only);
}

#ifdef WIN32
#define DIRECTORY_DIV   '\\'
static bool IsSlash(char ch_) { return ch_ == '/'; }
#else
#define DIRECTORY_DIV   '/'
#endif


bool http_text_protocol_header::send_file(const std::string& query_uri, network::connection& connect,
                                          const std::string& doc_root, const std::string& file_name) {
    using namespace boost::posix_time;

    auto logger = spdlog::get(KHL_LOGGER_COMMON);

    // TODO сделать корректировку по абсолютно-относительным переходам
    std::string sFileName;
    if (file_name.empty()) {
        sFileName = doc_root + query_uri;
    } else {
        sFileName = doc_root + file_name;
    }

#ifdef WIN32
    std::replace_if(sFileName.begin(), sFileName.end(), IsSlash, DIRECTORY_DIV);

    DWORD dwAttr = GetFileAttributes(sFileName.c_str());
#else
    struct stat s;
#endif

    if (*sFileName.rbegin() == DIRECTORY_DIV) {
        sFileName += "index.html";
#ifdef WIN32
    } else if (dwAttr != -1 && dwAttr & FILE_ATTRIBUTE_DIRECTORY) {
#else
    } else if (stat(sFileName.c_str(), &s)!=-1 && S_ISDIR(s.st_mode)) {
#endif
        sFileName.append(1, DIRECTORY_DIV);
        sFileName += "index.html";
    }

    std::string sCanonicFileName;

    if (sFileName.length() >= MAX_PATH) {
        logger->warn("Path is too long");

        set_response_status(http_response_status_not_found, "Not found");
        send_response(connect, "File not found");
        return false;
    }

    char bufCanonicFileName[MAX_PATH];
    memset(bufCanonicFileName, 0, sizeof(bufCanonicFileName));

#ifdef WIN32
    if (!PathCanonicalize((LPSTR)bufCanonicFileName, sFileName.c_str())) {
        logger->warn("PathCanonicalize failed");

        set_response_status(http_response_status_not_found, "Not found");
        send_response(connect, "File not found");
        return false;
    }
#else
    if (!realpath(sFileName.c_str(), bufCanonicFileName)) {
        logger->warn("realpath failed");

        set_response_status(http_response_status_not_found, "Not found");
        send_response(connect, "File not found");
        return false;
    }
#endif

    sCanonicFileName = bufCanonicFileName;

    if (sCanonicFileName.substr(0, doc_root.length()) != doc_root) {
        logger->warn("Access outside of docroot attempted");

        set_response_status(http_response_status_not_found, "Not found");
        send_response(connect, "File not found");
        return false;
    }

    system::fastfile ff;
    if (ff.open_file(sCanonicFileName, -1, true)) {
        static struct SECT {
            const char* m_Ext;
            const char* m_CT;
            const char* m_CP;
        } s_SECT[] = {
                {"js", HTTP_ATTRIBUTE_CONTENT_TYPE__APP_JS, HTTP_CODEPAGE_UTF8},
                {"html", HTTP_ATTRIBUTE_CONTENT_TYPE__TEXT_HTML, HTTP_CODEPAGE_UTF8},
                {"htm", HTTP_ATTRIBUTE_CONTENT_TYPE__TEXT_HTML, HTTP_CODEPAGE_UTF8},
                {"css", HTTP_ATTRIBUTE_CONTENT_TYPE__TEXT_CSS, HTTP_CODEPAGE_UTF8},
                {"gif", HTTP_ATTRIBUTE_CONTENT_TYPE__IMAGE_GIF, HTTP_CODEPAGE_NULL},
                {"jpg", HTTP_ATTRIBUTE_CONTENT_TYPE__IMAGE_JPG, HTTP_CODEPAGE_NULL},
                {"jpeg", HTTP_ATTRIBUTE_CONTENT_TYPE__IMAGE_JPG, HTTP_CODEPAGE_NULL},
                {"png", HTTP_ATTRIBUTE_CONTENT_TYPE__IMAGE_PNG, HTTP_CODEPAGE_NULL}, {nullptr, nullptr, nullptr}
            };

        const char* ims = get_header_parameter("If-Modified-Since", nullptr);
        if (ims != nullptr) {
            tm t;
            (ims, "%a, %d-%b-%Y %H:%M:%S GMT", &t);
#ifdef WIN32
            time_t tt = _mkgmtime(&t);
#else
            time_t tt = timegm(&t);
#endif  // WIN32
            if (tt >= ff.get_time_update()) {
                logger->debug("[HTTP] Don't send file '{}' length = {:d}. Response 304 (If-Modified-Since: '{}')",
                              query_uri.c_str(), ff.get_length(), ims);

                set_response_status(304, "Not Modified");
                send_response(connect, nullptr, 0);
                return true;
            }
        }

        const char* pExt = sCanonicFileName.c_str();
        size_t nExt = sCanonicFileName.size();
        for (; nExt > 0; --nExt) {
            if (pExt[nExt] == '.') {
                nExt++;
                break;
            } else if (pExt[nExt] == '\\' || pExt[nExt] == '/') {
                nExt = 0;
                break;
            }
        }

        logger->debug("[HTTP] Send file '{}' length = {:d}", query_uri.c_str(), ff.get_length());

        if (nExt > 0) {
            pExt += nExt;
            for (int k = 0; s_SECT[k].m_Ext != nullptr; ++k) {
                if (strcmp(s_SECT[k].m_Ext, pExt) == 0) {
                    set_content_type(s_SECT[k].m_CT, s_SECT[k].m_CP);
                    break;
                }
            }
        }

        set_last_modify(from_time_t(ff.get_time_update()));
        send_response(connect, nullptr, ff.get_length());

        if (is_send_data()) {
            // TODO добавить вариант отправки чанками
            connect.send_data(reinterpret_cast<const boost::uint8_t*>(ff.get_memory()), ff.get_length());
        }

        ff.close_file();
    } else {
        logger->warn("[HTTP] File not found '{}'", sCanonicFileName.c_str());

        set_response_status(http_response_status_not_found, "Not found");
        send_response(connect, "File not found");
        return false;
    }

    return true;
}

bool http_text_protocol_header::get_parameter(const std::string& key, const bool default_value) const {
    const auto value = get_parameter(key, nullptr);
    return value != nullptr ? strcmp(value, "true") == 0 : default_value;
}

int http_text_protocol_header::get_parameter(const std::string& key, const int default_value) const {
    const auto value = get_parameter(key, nullptr);
    return value != nullptr ? atoi(value) : default_value;
}

int64_t http_text_protocol_header::get_parameter64(const std::string& key, const int64_t default_value) const {
    const auto value = get_parameter(key, nullptr);
    return value != nullptr ? atoll(value) : default_value;
}

void http_text_protocol_header::calculate_host_port() {
    auto idx = get_header_index(HTTP_ATTRIBUTE_HOST);
    if (idx == std::string::npos) {
        return;
    }

    m_nHost = idx;

    for (;; ++idx) {
        const auto ch = *m_abHeader.get_position(idx);

        if (ch == '\0') {
            m_nPort = 0;
            break;
        }

        if (ch == ':') {
            m_abHeader[idx] = '\0';
            m_nPort = idx + 1;
            break;
        }
    }
}

const char* http_text_protocol_header::get_host() {
    if (m_nHost == std::string::npos) {
        calculate_host_port();
    }
    return m_nHost == std::string::npos ? "" : m_abHeader.get_position(m_nHost);
}

const char* http_text_protocol_header::get_port() {
    if (m_nHost == std::string::npos) {
        calculate_host_port();
    }
    return (m_nPort == std::string::npos || m_nPort == 0) ? "" : m_abHeader.get_position(m_nPort);
}

const char* http_text_protocol_header::get_client_proxy_ip() {
    auto idx = get_header_index(HTTP_ATTRIBUTE_X_FORWARDED_FOR);
    if (idx == std::string::npos) {
        idx = get_header_index(HTTP_ATTRIBUTE_X_REAL_IP);
        if (idx == std::string::npos) {
            return nullptr;
        }
    }
    return m_abHeader.get_position(idx);
}

static size_t sWriteAutobufferCallback(void* Contents_, size_t nSize_, size_t nMemb_, void* Ctx_) {
    size_t nRealSize = (nSize_ * nMemb_) / sizeof(char);
    data::auto_buffer_t<char>* pBuffer = reinterpret_cast<data::auto_buffer_t<char>*>(Ctx_);

    pBuffer->check_size(pBuffer->get_fill_size() + nRealSize);
    pBuffer->append(reinterpret_cast<const char*>(Contents_), nRealSize);

    return nRealSize * sizeof(char);
}

std::string http_curl_string::do_post_request(const std::string& uri, const std::string& request) const  {
    data::auto_buffer_char response;
    struct curl_slist* headers = nullptr;

    headers = curl_slist_append(headers, "Content-Type:application/json");
    CURL*       curl = curl_easy_init();

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 0L); /* allow connections to be reused */
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "rewtas agent");
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 0);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60 * 60L); /* timeout of 60 minutes */
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);

    curl_easy_setopt(curl, CURLOPT_URL, uri.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, static_cast<void*>(&response));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, sWriteAutobufferCallback);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, request.empty() ? 0 : request.size());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.empty() ? nullptr : request.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1);

    CURLcode response_code, ret;
    ret = curl_easy_perform(curl);
    const auto err = curl_easy_strerror(ret);

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_cleanup(curl);

    std::string body;
    if (response.get_fill_size() != 0) {
        body.assign(response.get_head(), response.get_fill_size());
    }
    return std::move(body);
}
