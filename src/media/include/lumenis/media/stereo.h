#pragma once

#include <QImage>
#include <QSize>

// Frame-compatible stereoscopic pictures (Editing Guide p.66; 8.6d1, docs/spec/stereoscopic.md): the project's Side by
// Side and Over / Under modes put both eyes in one picture.
namespace lumenis::media {

// "Side by Side: Frame compatible format that uses the left and right eye images one beside the other using horizontal
// half res for each eye"; "Over / Under: ... one over the other using vertical half res for each eye" (p.66). The left
// eye goes left or on top (ours: the guide does not say which). The result is `size` (the left eye's size when invalid);
// a null eye leaves its half black.
QImage frameCompatible(const QImage& left, const QImage& right, bool sideBySide, QSize size = {});

} // namespace lumenis::media
