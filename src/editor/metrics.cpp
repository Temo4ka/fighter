#include "editor/metrics.hpp"

#include <format>

#include "combat/events.hpp"
#include "editor/trajectory.hpp"

namespace fighter::editor {

std::vector<MetricLine> listMetrics(const combat::MoveMeasure& Measure) {
    const bool Done = Measure.Started && Measure.Finished && Measure.Problem.empty();
    const auto When = [Done](std::string Text) { return Done ? std::move(Text) : std::string("-"); };
    std::vector<MetricLine> Lines;
    Lines.push_back({"problem", Measure.Problem.empty() ? std::string("-") : Measure.Problem});
    Lines.push_back({"startup", When(std::format("{:.2f} s", Measure.StartupSec))});
    Lines.push_back({"active", When(std::format("{:.2f} s", Measure.ActiveSec))});
    Lines.push_back({"recovery", When(std::format("{:.2f} s", Measure.RecoverySec))});
    Lines.push_back({"total", When(std::format("{:.2f} s", Measure.getTotalSec()))});

    const combat::StrikerPath* Farthest = Measure.findFarthestPath();
    Lines.push_back({"reach", When(Farthest && Measure.ActiveSec > 0.0f
                                       ? std::format("{:.2f} m ({})", Farthest->ReachM, getBodyPartName(Farthest->Part))
                                       : std::string("no active phase"))});
    const TipSpeed Speed = findTipSpeed(Measure);
    Lines.push_back({"tip speed", When(Speed.PeakMps > 0.0f ? std::format("peak {:.1f} m/s, mean {:.1f} m/s",
                                                                           Speed.PeakMps, Speed.ActiveMeanMps)
                                                            : std::string("-"))});
    Lines.push_back({"pelvis", When(std::format("{:+.2f} / {:+.2f} m, ends {:+.2f} m", Measure.PelvisForwardM,
                                                Measure.PelvisBackM, Measure.PelvisEndM))});
    Lines.push_back({"hit", When(Measure.Hit ? std::format("{} at {:.2f} s{}, damage {:.1f}, {}",
                                                           getBodyPartName(Measure.HitPart), Measure.HitTimeSec,
                                                           Measure.Blocked ? " (blocked)" : "", Measure.Damage,
                                                           combat::getReactionLevelName(Measure.Reaction))
                                             : std::string("no hit"))});
    Lines.push_back({"contact", When(Measure.Hit ? std::format("{:.2f} m/s, J {:.1f} N*s, strength {:.2f}",
                                                               Measure.ApproachSpeed, Measure.Impulse, Measure.Strength)
                                                 : std::string("-"))});
    return Lines;
}

} // namespace fighter::editor
