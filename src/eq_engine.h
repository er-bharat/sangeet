#pragma once

// eq_engine.h
//
// Everything the equalizer needs that is NOT a widget:
//
//   1. The data model          (Eq::Band, Eq::State, Eq::Preset)
//   2. The curve math          (Eq::totalResponseDb) - draws exactly what
//                              ffmpeg's `equalizer` filter will do
//   3. JSON (de)serialisation  - for persistence and presets
//   4. Eq::MpvBridge           - turns an Eq::State into libmpv commands
//
// The EQ window (eq_window.h) edits an Eq::State and emits
// (state, Eq::Change, bandIndex). PlayerPage forwards that straight to
// Eq::MpvBridge::apply(), which decides what mpv needs to hear.
//
// mpv side: each dot is one labelled lavfi filter in the `af` chain:
//
//   @preamp:lavfi=[volume=0.707946],
//   @eq0:lavfi=[equalizer=f=1000.000:t=q:w=1.000:g=3.000],
//   @eq1:lavfi=[equalizer=f=60.000:t=q:w=0.800:g=6.000],
//   ...,
//   @limiter:lavfi=[alimiter=limit=0.98:level=0]
//
// Structural changes (dot added/removed, preset loaded, EQ on/off) rebuild the
// chain with `set af ...`. Dragging a dot or moving the preamp only sends
// `af-command <label> <param> <value>`, which retunes the running filter
// without rebuilding the chain.

#include <QByteArray>
#include <QByteArrayList>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <algorithm>
#include <cmath>
#include <initializer_list>

#include <mpv/client.h>

namespace Eq {

// ---------------------------------------------------------------- limits ---

constexpr double kMinFreq = 20.0;
constexpr double kMaxFreq = 20000.0;
constexpr double kMaxGainDb = 15.0;     // per band and for the preamp (+/-)
constexpr double kMinQ = 0.2;
constexpr double kMaxQ = 12.0;
constexpr double kDefaultQ = 1.0;
constexpr int kMaxBands = 16;
constexpr double kCurveSampleRate = 48000.0;  // only used to draw the curve
constexpr double kPi = 3.14159265358979323846;
constexpr bool kUseLimiter = true;      // safety net against clipping on boosts

inline double clampFreq(double f) { return std::clamp(f, kMinFreq, kMaxFreq); }
inline double clampGain(double g) { return std::clamp(g, -kMaxGainDb, kMaxGainDb); }
inline double clampQ(double q) { return std::clamp(q, kMinQ, kMaxQ); }

// ----------------------------------------------------------------- model ---

struct Band {
    double freq = 1000.0;
    double gainDb = 0.0;
    double q = kDefaultQ;
};

struct State {
    bool enabled = true;
    double preampDb = 0.0;
    QList<Band> bands;        // index == mpv filter label (eq0, eq1, ...)
    QString presetName;       // empty == "Custom" (edited / unsaved)
};

struct Preset {
    QString name;
    State state;              // only preampDb + bands are meaningful
    bool builtin = false;
};
using PresetList = QList<Preset>;

// What changed, so the bridge can send the cheapest correct update.
enum class Change {
    Enabled,      // EQ switched on/off      -> rebuild (or clear) chain
    Preamp,       // preamp slider           -> af-command
    BandParams,   // a dot moved / Q changed -> af-command (needs band index)
    Structure,    // dot added/removed, preset loaded -> rebuild chain
    Meta          // preset name only        -> nothing for mpv
};

inline QString formatFreq(double f)
{
    if (f >= 1000.0)
        return QString::number(f / 1000.0, 'f', f >= 10000.0 ? 1 : 2) + " kHz";
    return QString::number(qRound(f)) + " Hz";
}

// ------------------------------------------------------------ curve math ---
//
// ffmpeg's `equalizer` (t=q) is the RBJ peaking biquad. The magnitude below
// is the same filter, so the drawn curve matches what you hear.

inline double bandResponseDb(const Band &b, double freq,
                             double sampleRate = kCurveSampleRate)
{
    const double A = std::pow(10.0, b.gainDb / 40.0);
    const double w0 = 2.0 * kPi * b.freq / sampleRate;
    const double alpha = std::sin(w0) / (2.0 * b.q);
    const double cw = std::cos(w0);

    const double b0 = 1.0 + alpha * A, b1 = -2.0 * cw, b2 = 1.0 - alpha * A;
    const double a0 = 1.0 + alpha / A, a1 = -2.0 * cw, a2 = 1.0 - alpha / A;

    const double w = 2.0 * kPi * freq / sampleRate;
    const double c1 = std::cos(w), c2 = std::cos(2.0 * w);
    const double s1 = std::sin(w), s2 = std::sin(2.0 * w);

    auto mag2 = [&](double x0, double x1, double x2) {
        const double re = x0 + x1 * c1 + x2 * c2;
        const double im = -(x1 * s1 + x2 * s2);
        return re * re + im * im;
    };
    return 10.0 * std::log10(mag2(b0, b1, b2) / mag2(a0, a1, a2));
}

// Sum of all bands (dB). The preamp is a flat offset and is not included.
inline double totalResponseDb(const State &s, double freq)
{
    double db = 0.0;
    for (const Band &b : s.bands)
        db += bandResponseDb(b, freq);
    return db;
}

// --------------------------------------------------------- serialisation ---

inline QJsonObject bandToJson(const Band &b)
{
    return {{"f", b.freq}, {"g", b.gainDb}, {"q", b.q}};
}

inline Band bandFromJson(const QJsonObject &o)
{
    Band b;
    b.freq = clampFreq(o.value("f").toDouble(1000.0));
    b.gainDb = clampGain(o.value("g").toDouble(0.0));
    b.q = clampQ(o.value("q").toDouble(kDefaultQ));
    return b;
}

inline QJsonObject stateToObject(const State &s)
{
    QJsonArray bands;
    for (const Band &b : s.bands)
        bands.append(bandToJson(b));
    return {{"enabled", s.enabled},
            {"preamp", s.preampDb},
            {"bands", bands},
            {"preset", s.presetName}};
}

inline State stateFromObject(const QJsonObject &o)
{
    State s;
    s.enabled = o.value("enabled").toBool(true);
    s.preampDb = clampGain(o.value("preamp").toDouble(0.0));
    s.presetName = o.value("preset").toString();
    const QJsonArray bands = o.value("bands").toArray();
    for (const QJsonValue &v : bands) {
        if (s.bands.size() >= kMaxBands)
            break;
        s.bands.append(bandFromJson(v.toObject()));
    }
    return s;
}

inline QByteArray stateToJson(const State &s)
{
    return QJsonDocument(stateToObject(s)).toJson(QJsonDocument::Compact);
}

inline State stateFromJson(const QByteArray &json, bool *ok = nullptr)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    const bool good = err.error == QJsonParseError::NoError && doc.isObject();
    if (ok)
        *ok = good;
    return good ? stateFromObject(doc.object()) : State{};
}

inline QByteArray presetsToJson(const PresetList &presets)
{
    QJsonArray arr;
    for (const Preset &p : presets)
        arr.append(QJsonObject{{"name", p.name}, {"state", stateToObject(p.state)}});
    return QJsonDocument(arr).toJson(QJsonDocument::Compact);
}

inline PresetList presetsFromJson(const QByteArray &json)
{
    PresetList result;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isArray())
        return result;
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        Preset p;
        p.name = o.value("name").toString();
        p.state = stateFromObject(o.value("state").toObject());
        if (!p.name.isEmpty())
            result.append(p);
    }
    return result;
}

// -------------------------------------------------------- built-in presets ---

inline PresetList builtinPresets()
{
    auto make = [](const QString &name, double preamp,
                   std::initializer_list<Band> bands) {
        Preset p;
        p.name = name;
        p.builtin = true;
        p.state.preampDb = preamp;
        p.state.bands = QList<Band>(bands);
        return p;
    };

    return {
        make("Flat", 0.0, {}),
        make("Bass Boost", -3.0, {{60.0, 6.0, 0.8}, {150.0, 3.0, 1.0}}),
        make("Treble Boost", -3.0, {{6000.0, 3.0, 0.8}, {12000.0, 5.0, 0.7}}),
        make("Vocal", -1.0, {{250.0, -2.0, 1.0}, {2500.0, 3.0, 1.0}, {5000.0, 2.0, 1.0}}),
        make("Rock", -3.0, {{80.0, 4.0, 0.9}, {400.0, -2.0, 1.0}, {3000.0, 2.0, 1.0}, {10000.0, 4.0, 0.8}}),
        make("Loudness", -4.0, {{60.0, 5.0, 0.8}, {10000.0, 4.0, 0.8}}),
    };
}

// ------------------------------------------------------------ mpv bridge ---

class MpvBridge
{
public:
    // Full filter chain string for the mpv `af` property. Empty when the EQ
    // is switched off, which removes every filter (true bypass).
    static QByteArray filterChain(const State &s)
    {
        if (!s.enabled)
            return {};

        QByteArrayList parts;
        parts << preampFilter(s.preampDb);
        for (int i = 0; i < s.bands.size(); ++i)
            parts << bandFilter(i, s.bands[i]);
        if (kUseLimiter)
            parts << QByteArray("@limiter:lavfi=[alimiter=limit=0.98:level=0]");
        return parts.join(',');
    }

    // Translate one EQ-window change into the smallest set of mpv commands.
    static void apply(mpv_handle *mpv, const State &s, Change change, int band = -1)
    {
        if (!mpv)
            return;

        switch (change) {
        case Change::Meta:
            return;

        case Change::Preamp:
            if (s.enabled)
                afCommand(mpv, "preamp", "volume", number(dbToLinear(s.preampDb), 6));
            return;

        case Change::BandParams:
            if (s.enabled && band >= 0 && band < s.bands.size()) {
                const QByteArray label = bandLabel(band);
                const Band &b = s.bands[band];
                afCommand(mpv, label, "frequency", number(b.freq, 3));
                afCommand(mpv, label, "width", number(b.q, 3));
                afCommand(mpv, label, "gain", number(b.gainDb, 3));
            }
            return;

        case Change::Enabled:
        case Change::Structure:
            setAf(mpv, filterChain(s));
            return;
        }
    }

    static QByteArray bandLabel(int index)
    {
        return "eq" + QByteArray::number(index);
    }

private:
    static double dbToLinear(double db) { return std::pow(10.0, db / 20.0); }

    static QByteArray number(double v, int precision)
    {
        return QByteArray::number(v, 'f', precision);   // always '.' decimal
    }

    static QByteArray preampFilter(double db)
    {
        return "@preamp:lavfi=[volume=" + number(dbToLinear(db), 6) + "]";
    }

    static QByteArray bandFilter(int index, const Band &b)
    {
        return "@" + bandLabel(index) + ":lavfi=[equalizer=f=" + number(b.freq, 3)
               + ":t=q:w=" + number(b.q, 3) + ":g=" + number(b.gainDb, 3) + "]";
    }

    static void setAf(mpv_handle *mpv, const QByteArray &chain)
    {
        const char *cmd[] = {"set", "af", chain.constData(), nullptr};
        mpv_command_async(mpv, 0, cmd);
    }

    static void afCommand(mpv_handle *mpv, const QByteArray &label,
                          const char *name, const QByteArray &value)
    {
        const char *cmd[] = {"af-command", label.constData(), name,
                             value.constData(), nullptr};
        mpv_command_async(mpv, 0, cmd);
    }
};

} // namespace Eq
