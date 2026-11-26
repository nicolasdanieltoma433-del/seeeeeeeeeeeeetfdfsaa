#pragma once
// ============================================================================
//  TickSaver · ui/DeathSummaryNode.hpp
//  Panel que aparece al morir (~2.5 s, luego se desvanece solo):
//
//        RESUMEN  ·  47%
//    Salvados: 3   Mejor racha: x12 (nivel x30)
//    Ultimo clic: temprano por 3t: salvado
// ============================================================================

#include "../core/Stats.hpp"
#include "../core/Types.hpp"

#include <Geode/Geode.hpp>

namespace ts::ui {

struct DeathSummary {
    float percent = 0.f;
    int savesThisAttempt = 0;
    int bestStreakThisAttempt = 0;
    int bestStreakLevel = 0;
    int totalSavesLevel = 0;
    LastClick last{};
};

class DeathSummaryNode : public cocos2d::CCLayerColor {
public:
    static DeathSummaryNode* create(DeathSummary const& summary, Palette const& palette, float scale);

protected:
    bool initSummary(DeathSummary const& summary, Palette const& palette, float scale);
};

} // namespace ts::ui
