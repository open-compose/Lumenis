// The OFX host's colour conversions (6.11c3, docs/spec/ofx-host.md "Colour management"): a thin wrapper over
// OpenColorIO, so that ofx_host.cpp does not depend on its headers. In a build without OpenColorIO (LUMENIS_HAVE_OCIO
// unset; it is optional, cmake/FindOpenColorIO.cmake) every function here says there is no config and converts nothing.
#pragma once

#include <QString>

#include <cstddef>

namespace lumenis::media::detail {

// Whether this build has OpenColorIO, and its version ("2.1.3"); empty without it.
bool ocioBuilt();
QString ocioVersion();

// The config to use: a file or an ocio:// URI. Empty (the default) means automatic: the OCIO environment variable's,
// else the built-in ACES studio config the OFX native config header was made from, where the library has it (2.3 and
// later; 2.2's older studio config otherwise). LUMENIS_NO_OCIO=1 turns colour conversion off; it is read when the
// config is next loaded, so set it before calling this.
void setOcioConfig(const QString& config);
// The config in use, loaded on first use; empty when there is none (no library, LUMENIS_NO_OCIO, nothing found, or
// it failed to load; `error` says why when it failed).
QString ocioConfig(QString* error = nullptr);

// The config's name for colour space or role `name` (OCIO's getColorSpace: names, aliases and roles); empty when the
// config has none or there is no config.
QString ocioColourSpace(const QString& name);
// Whether the config's colour space `name` holds data rather than colour (OCIO's isData, e.g. "Raw"); false when unknown.
bool ocioIsData(const QString& name);

// Converts `pixels` premultiplied float RGBA pixels in place from colour space `from` to `to` (names the config
// knows). Alpha is kept; colour is unpremultiplied around the conversion where alpha is between 0 and 1. Returns false
// (and leaves the pixels alone) when it cannot, with the reason in `error`.
bool ocioConvert(float* rgba, std::size_t pixels, const QString& from, const QString& to, QString* error = nullptr);

// "You can import CLF (Common LUT Format) files" (Editing Guide p.542; 8.5d2): the file's transform sampled on a cube of
// `size` points a side (red changing fastest), RGB in `rgb` (size^3 * 3 floats). False, with `error`, when OpenColorIO
// is not built in or cannot read the file. Needs no config.
bool ocioBakeFile(const QString& path, int size, float* rgb, QString* error = nullptr);

} // namespace lumenis::media::detail
