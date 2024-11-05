module;
#include <base/Assert.hh>
#include <base/Macros.hh>
#include <webp/decode.h>
#include <pr/gl-headers.hh>
module pr.client.render.gl;

using namespace gl;
using namespace pr;
using namespace pr::client;

constexpr u8 DefaultTextureData[] {
#embed "default_texture.webp"
};

auto GetDefaultTexture() -> DrawableTexture {
    int wd, ht;
    auto data = WebPDecodeRGBA(DefaultTextureData, sizeof DefaultTextureData, &wd, &ht);
    Assert(data, "Failed to decode embedded image?");
    defer { WebPFree(data); };
    return DrawableTexture(data, wd, ht, GL_RGBA, GL_UNSIGNED_BYTE, true);
}

auto DrawableTexture::LoadFromFile(File::PathRef path) -> DrawableTexture {
    auto file = File::Read(path);
    if (not file) {
        Log("Could not read file '{}': {}", path.string(), file.error());
        return GetDefaultTexture();
    }

    int wd, ht;
    auto data = WebPDecodeRGBA(reinterpret_cast<const u8*>(file.value().data()), file.value().size(), &wd, &ht);
    if (not data) {
        Log("Could not decode image '{}'", path.string());
        return GetDefaultTexture();
    }

    defer { WebPFree(data); };
    return DrawableTexture(data, wd, ht, GL_RGBA, GL_UNSIGNED_BYTE);
}