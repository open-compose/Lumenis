#pragma once

#include <QJsonObject>
#include <QString>

// The effect value kept in sequences (model::Effect): its own header so that what a segment holds besides its effects,
// such as a spatial adapter's FrameFlex effect (lumenis/model/frame_flex.h), can hold one too. Its JSON functions are
// in sequence.cpp.
namespace lumenis::model {

// An effect in a sequence (FX guide ch. 2): its category and name in the
// Effect Palette (p.37) and the parameter values changed in the Effect
// Editor (p.83), by parameter category and name.
struct Effect {
    QString category; // "Blend"
    QString name;     // "Dissolve"
    // Parameter category -> {parameter name -> value}; empty when every parameter has its default.
    QJsonObject parameters;

    // {category, name, parameters?}; fromJson() takes missing members as empty.
    QJsonObject toJson() const;
    static Effect fromJson(const QJsonObject& o);
    bool operator==(const Effect&) const = default;
};

} // namespace lumenis::model
