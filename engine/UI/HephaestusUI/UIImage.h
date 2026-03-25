#pragma once

#include "UIElement.h"

#include <string>

namespace Hephaestus::HephaestusUI {

class UIImage : public UIElement {
public:
    UIImage() = default;
    explicit UIImage(std::string imageId) : m_ImageId(std::move(imageId)) {}

    void SetImageId(std::string imageId) { m_ImageId = std::move(imageId); }
    void SetTint(const UIColor& tint) { m_Tint = tint; }
    void SetScaleMode(UIImageScaleMode scaleMode) { m_ScaleMode = scaleMode; }

    [[nodiscard]] const std::string& GetImageId() const { return m_ImageId; }
    [[nodiscard]] UIImageScaleMode GetScaleMode() const { return m_ScaleMode; }

protected:
    void OnEmitDrawCommands(UIRenderer& renderer) const override;

private:
    std::string m_ImageId;
    UIColor m_Tint = UIColor::White();
    UIImageScaleMode m_ScaleMode = UIImageScaleMode::Stretch;
};

} // namespace Hephaestus::HephaestusUI
