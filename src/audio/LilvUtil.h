#pragma once
#include <lilv/lilv.h>
#include <string>

// Lilv returns a null node whenever a bundle omits something mandatory (a
// plugin without doap:name, a port without lv2:name) and a null string for a
// null node.  std::string, strcmp and lilv_file_uri_parse all dereference what
// they are given, so every node coming out of Lilv passes through here first.

inline const char* lilvText(const LilvNode* node) {
    const char* text = node ? lilv_node_as_string(node) : nullptr;
    return text ? text : "";
}

inline const char* lilvUriText(const LilvNode* node) {
    const char* uri = (node && lilv_node_is_uri(node)) ? lilv_node_as_uri(node) : nullptr;
    return uri ? uri : "";
}

inline std::string lilvString(const LilvNode* node) {
    return std::string(lilvText(node));
}

// For the get_name family, which hands back a node the caller owns.
inline std::string lilvTakeString(LilvNode* node) {
    std::string value(lilvText(node));
    lilv_node_free(node);
    return value;
}

// Display name for a plugin whose bundle omits the mandatory doap:name.
inline std::string lilvNameFromUri(const std::string& uri) {
    const auto cut = uri.find_last_of("/#");
    const std::string tail = (cut != std::string::npos) ? uri.substr(cut + 1) : uri;
    return tail.empty() ? std::string("Unnamed plugin") : tail;
}

inline std::string lilvFilePath(const LilvNode* node) {
    std::string path;
    if (!node || !lilv_node_is_uri(node)) return path;
    if (char* parsed = lilv_file_uri_parse(lilv_node_as_uri(node), nullptr)) {
        path = parsed;
        lilv_free(parsed);
    }
    return path;
}
