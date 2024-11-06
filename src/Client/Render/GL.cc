module;
#include <base/Assert.hh>
#include <base/Macros.hh>
#include <pr/gl-headers.hh>
#include <webp/decode.h>
module pr.client.render.gl;
import pr.utils;

using namespace gl;
using namespace pr;
using namespace pr::client;

constexpr u8 DefaultTextureData[]{
#embed "default_texture.webp"
};

auto GetDefaultTexture() -> DrawableTexture {
    int wd, ht;
    auto data = WebPDecodeRGBA(DefaultTextureData, sizeof DefaultTextureData, &wd, &ht);
    Assert(data, "Failed to decode embedded image?");
    defer { WebPFree(data); };
    return DrawableTexture(data, wd, ht, GL_RGBA, GL_UNSIGNED_BYTE, true);
}

auto DrawableTexture::LoadFromFile(fs::PathRef path) -> DrawableTexture {
    auto file = File::Read(path);
    if (not file) {
        Log("Could not read file '{}': {}", path.string(), file.error());
        return GetDefaultTexture();
    }

    int wd, ht;
    auto data = WebPDecodeRGBA(file.value().data<u8>(), file.value().size(), &wd, &ht);
    if (not data) {
        Log("Could not decode image '{}'", path.string());
        return GetDefaultTexture();
    }

    defer { WebPFree(data); };
    return DrawableTexture(data, wd, ht, GL_RGBA, GL_UNSIGNED_BYTE);
}
