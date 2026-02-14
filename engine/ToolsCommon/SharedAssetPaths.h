#pragma once

#include <QString>

namespace Hephaestus::Tools {

class SharedAssetPaths {
public:
    static SharedAssetPaths Locate();

    QString root;
    QString iconsDir;
    QString imagesDir;
    QString fontsDir;
    QString stylesDir;
    QString i18nDir;
    QString thumbnailsDir;

    QString Resolve(const QString& relative) const;
    bool IsValid() const;
};

} // namespace Hephaestus::Tools
