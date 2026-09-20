#include "audio/LilvUtil.h"
#include <cassert>
#include <iostream>

// A bundle that omits something mandatory makes Lilv hand back a null node, and
// a null node makes lilv_node_as_string return a null pointer.  Feeding that to
// std::string used to crash the plugin scan on startup.
int main() {
    assert(lilvText(nullptr) != nullptr);
    assert(std::string(lilvText(nullptr)).empty());
    assert(lilvUriText(nullptr) != nullptr);
    assert(std::string(lilvUriText(nullptr)).empty());
    assert(lilvString(nullptr).empty());
    assert(lilvTakeString(nullptr).empty());
    assert(lilvFilePath(nullptr).empty());

    assert(lilvNameFromUri("http://example.org/plugins/Chorus") == "Chorus");
    assert(lilvNameFromUri("http://example.org/plugins#Chorus") == "Chorus");
    assert(lilvNameFromUri("urn:example:chorus") == "urn:example:chorus");
    assert(lilvNameFromUri("http://example.org/plugins/") == "Unnamed plugin");
    assert(lilvNameFromUri("") == "Unnamed plugin");

    // A real world is cheap enough to confirm the helpers against live nodes.
    LilvWorld* world = lilv_world_new();
    LilvNode* uri = lilv_new_uri(world, "http://example.org/plugin");
    LilvNode* literal = lilv_new_string(world, "not a uri");
    assert(lilvString(uri) == "http://example.org/plugin");
    assert(std::string(lilvUriText(uri)) == "http://example.org/plugin");
    assert(std::string(lilvUriText(literal)).empty());  // a literal is not a URI
    assert(lilvString(literal) == "not a uri");
    assert(lilvFilePath(literal).empty());

    LilvNode* fileUri = lilv_new_uri(world, "file:///tmp/bundle.lv2/");
    assert(lilvFilePath(fileUri) == "/tmp/bundle.lv2/");

    lilv_node_free(fileUri);
    lilv_node_free(literal);
    lilv_node_free(uri);
    lilv_world_free(world);

    std::cout << "LilvUtil tests passed" << std::endl;
    return 0;
}
