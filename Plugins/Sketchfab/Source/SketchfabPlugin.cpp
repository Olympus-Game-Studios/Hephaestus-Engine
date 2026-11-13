#include "PluginAPI/PluginAPI.h"
#include "../../Common/AssetImportText.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPixmap>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <map>
#include <string>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <wincred.h>
#endif

using namespace Hephaestus::Plugins;

namespace {

constexpr const char* kApi = "https://api.sketchfab.com/v3";
constexpr const char* kTokenPage = "https://sketchfab.com/settings/password";

#ifdef _WIN32
constexpr const wchar_t* kCredentialTarget = L"Hephaestus Engine/Sketchfab API token";
#endif

// The API token lives in the operating system's credential store, never in project or editor settings.
QString LoadToken() {
#ifdef _WIN32
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(kCredentialTarget, CRED_TYPE_GENERIC, 0, &credential)) {
        return {};
    }
    const QString token = QString::fromUtf8(reinterpret_cast<const char*>(credential->CredentialBlob),
                                            static_cast<qsizetype>(credential->CredentialBlobSize));
    CredFree(credential);
    return token;
#else
    return {};
#endif
}

bool SaveToken(const QString& token, QString& error) {
#ifdef _WIN32
    QByteArray blob = token.toUtf8();
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(kCredentialTarget);
    credential.UserName = const_cast<LPWSTR>(L"api-token");
    credential.CredentialBlobSize = static_cast<DWORD>(blob.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(blob.data());
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    if (!CredWriteW(&credential, 0)) {
        error = QObject::tr("Windows Credential Manager refused the token (error %1).").arg(GetLastError());
        return false;
    }
    return true;
#else
    (void)token;
    error = QObject::tr("Saving the token needs the Windows Credential Manager, which this platform does not have.");
    return false;
#endif
}

void DeleteToken() {
#ifdef _WIN32
    CredDeleteW(kCredentialTarget, CRED_TYPE_GENERIC, 0);
#endif
}

QString LicenseLabel(const QJsonObject& model) { return model["license"].toObject()["label"].toString(); }

QString LicenseUrl(const QJsonObject& model) {
    const QString url = model["license"].toObject()["url"].toString();
    if (!url.isEmpty()) {
        return url;
    }
    static const QList<QPair<QString, QString>> known = {
        {"CC Attribution-NonCommercial-ShareAlike", "http://creativecommons.org/licenses/by-nc-sa/4.0/"},
        {"CC Attribution-NonCommercial-NoDerivs", "http://creativecommons.org/licenses/by-nc-nd/4.0/"},
        {"CC Attribution-NonCommercial", "http://creativecommons.org/licenses/by-nc/4.0/"},
        {"CC Attribution-ShareAlike", "http://creativecommons.org/licenses/by-sa/4.0/"},
        {"CC Attribution-NoDerivs", "http://creativecommons.org/licenses/by-nd/4.0/"},
        {"CC Attribution", "http://creativecommons.org/licenses/by/4.0/"},
        {"CC0 Public Domain", "http://creativecommons.org/publicdomain/zero/1.0/"},
    };
    for (const auto& [name, link] : known) {
        if (LicenseLabel(model).compare(name, Qt::CaseInsensitive) == 0) {
            return link;
        }
    }
    return QStringLiteral("https://sketchfab.com/licenses");
}

bool IsNonCommercial(const QString& label) { return label.contains("NonCommercial", Qt::CaseInsensitive); }
bool IsNoDerivatives(const QString& label) { return label.contains("NoDerivs", Qt::CaseInsensitive); }

QString ThumbnailUrl(const QJsonObject& model) {
    QString best;
    int bestWidth = 0;
    for (const QJsonValue& image : model["thumbnails"].toObject()["images"].toArray()) {
        const int width = image["width"].toInt();
        if (best.isEmpty() || (width >= 200 && (bestWidth < 200 || width < bestWidth)) ||
            (bestWidth < 200 && width > bestWidth)) {
            best = image["url"].toString();
            bestWidth = width;
        }
    }
    return best;
}

QString Json(const QJsonObject& object) { return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)); }

/// Sketchfab's Data and Download APIs plus the import into the project, shared by the panel and the MCP tools.
class SketchfabClient final : public QObject {
public:
    using PageCallback = std::function<void(bool ok, const QJsonObject& page, const QString& error)>;
    using ProgressCallback = std::function<void(qint64 received, qint64 total)>;
    using DoneCallback = std::function<void(bool ok, const QString& summary, const AssetPackImportResult& result)>;

    explicit SketchfabClient(IPluginContext& context) : m_context(context), m_network(new QNetworkAccessManager(this)) {}

    QNetworkAccessManager* Network() const { return m_network; }

    static QUrl SearchUrl(const QString& text, int count) {
        QUrl url(QString(kApi) + "/search");
        QUrlQuery query;
        query.addQueryItem("type", "models");
        query.addQueryItem("downloadable", "true");
        query.addQueryItem("count", QString::number(count));
        if (!text.isEmpty()) {
            query.addQueryItem("q", text);
        }
        url.setQuery(query);
        return url;
    }

    /// GETs a public API URL (search pages, model details).
    void Get(const QUrl& url, PageCallback done) {
        QNetworkReply* reply = m_network->get(QNetworkRequest(url));
        connect(reply, &QNetworkReply::finished, this, [reply, done] {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                done(false, {}, reply->errorString());
                return;
            }
            done(true, QJsonDocument::fromJson(reply->readAll()).object(), {});
        });
    }

    /// Download with the saved API token, then import the pack into assets/Imported/Sketchfab/<name>.
    void Import(const QJsonObject& model, ProgressCallback progress, DoneCallback done) {
        const QString token = LoadToken();
        if (token.isEmpty()) {
            done(false, tr("No Sketchfab API token is saved. Add one in Window > Asset Stores > Sketchfab."), {});
            return;
        }
        if (!*m_context.Project().ProjectRoot()) {
            done(false, tr("Open a project before importing models."), {});
            return;
        }
        QNetworkRequest request(QUrl(QString(kApi) + "/models/" + model["uid"].toString() + "/download"));
        request.setRawHeader("Authorization", "Token " + token.toUtf8());
        QNetworkReply* reply = m_network->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, model, progress, done] {
            reply->deleteLater();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 401 || status == 403) {
                done(false, tr("Sketchfab did not accept the API token. Check it with Change token."), {});
                return;
            }
            if (reply->error() != QNetworkReply::NoError) {
                done(false, tr("Download request failed: %1").arg(reply->errorString()), {});
                return;
            }
            const QJsonObject formats = QJsonDocument::fromJson(reply->readAll()).object();
            const bool glb = formats.contains("glb");
            const QString url = formats[glb ? "glb" : "gltf"].toObject()["url"].toString();
            if (url.isEmpty()) {
                done(false, tr("Sketchfab offers no glTF download for this model."), {});
                return;
            }
            Download(model, QUrl(url), glb ? ".glb" : ".zip", progress, done);
        });
    }

private:
    void Download(const QJsonObject& model, const QUrl& url, const QString& extension, ProgressCallback progress,
                  DoneCallback done) {
        const QString name = QString::fromStdString(SafeFileName(model["name"].toString().toStdString()));
        const QString folder =
            QDir(QString::fromUtf8(m_context.Project().ProjectRoot())).filePath(".hephaestus/downloads/sketchfab");
        QDir().mkpath(folder);
        auto* file = new QFile(QDir(folder).filePath(model["uid"].toString() + "-" + name + extension), this);
        if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            done(false, tr("Could not write %1").arg(file->fileName()), {});
            delete file;
            return;
        }
        // The download link is pre-signed; the API token is never sent with it.
        QNetworkReply* reply = m_network->get(QNetworkRequest(url));
        connect(reply, &QNetworkReply::readyRead, this, [reply, file] { file->write(reply->readAll()); });
        connect(reply, &QNetworkReply::downloadProgress, this, [progress](qint64 received, qint64 total) {
            progress(received, total);
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, file, model, name, done] {
            reply->deleteLater();
            file->write(reply->readAll());
            file->close();
            const QString path = file->fileName();
            file->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                QFile::remove(path);
                done(false, tr("Download failed: %1").arg(reply->errorString()), {});
                return;
            }
            Intake(model, path, name, done);
        });
    }

    void Intake(const QJsonObject& model, const QString& path, const QString& name, const DoneCallback& done) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const QJsonObject user = model["user"].toObject();
        const QByteArray source = path.toUtf8();
        const QByteArray target = ("assets/Imported/Sketchfab/" + name).toUtf8();
        const QByteArray title = model["name"].toString().toUtf8();
        const QByteArray viewer = model["viewerUrl"].toString().toUtf8();
        const QByteArray author = user["displayName"].toString().toUtf8();
        const QByteArray profile = user["profileUrl"].toString().toUtf8();
        const QByteArray license = LicenseLabel(model).toUtf8();
        const QByteArray licenseUrl = LicenseUrl(model).toUtf8();
        AssetPackImportRequest request;
        request.sourcePath = source.constData();
        request.targetFolder = target.constData();
        request.title = title.constData();
        request.source = "Sketchfab";
        request.sourceUrl = viewer.constData();
        request.author = author.constData();
        request.authorUrl = profile.constData();
        request.license = license.constData();
        request.licenseUrl = licenseUrl.constData();
        AssetPackImportResult result;
        const bool ok = m_context.Assets().ImportAssetPack(request, result);
        QApplication::restoreOverrideCursor();
        QFile::remove(path);
        done(ok, ok ? ImportSummary(result) : tr("Import failed: %1").arg(QString::fromUtf8(result.message)), result);
    }

    IPluginContext& m_context;
    QNetworkAccessManager* m_network = nullptr;
};

class SketchfabPanel final : public QWidget {
public:
    SketchfabPanel(SketchfabClient& client, QWidget* parent) : QWidget(parent), m_client(&client) {
        auto* layout = new QVBoxLayout(this);

        auto* account = new QHBoxLayout();
        m_accountLabel = new QLabel(this);
        m_accountLabel->setWordWrap(true);
        m_tokenButton = new QPushButton(this);
        account->addWidget(m_accountLabel, 1);
        account->addWidget(m_tokenButton);
        layout->addLayout(account);

        auto* searchRow = new QHBoxLayout();
        m_query = new QLineEdit(this);
        m_query->setPlaceholderText(tr("Search downloadable Sketchfab models"));
        auto* searchButton = new QPushButton(tr("Search"), this);
        searchRow->addWidget(m_query, 1);
        searchRow->addWidget(searchButton);
        layout->addLayout(searchRow);

        m_results = new QListWidget(this);
        m_results->setViewMode(QListView::IconMode);
        m_results->setIconSize(QSize(160, 90));
        m_results->setGridSize(QSize(176, 140));
        m_results->setResizeMode(QListView::Adjust);
        m_results->setMovement(QListView::Static);
        m_results->setWordWrap(true);
        layout->addWidget(m_results, 1);

        m_moreButton = new QPushButton(tr("More results"), this);
        m_moreButton->setVisible(false);
        layout->addWidget(m_moreButton);

        m_details = new QLabel(this);
        m_details->setWordWrap(true);
        m_details->setTextFormat(Qt::RichText);
        m_details->setOpenExternalLinks(true);
        layout->addWidget(m_details);

        auto* actions = new QHBoxLayout();
        m_viewButton = new QPushButton(tr("View on Sketchfab"), this);
        m_importButton = new QPushButton(tr("Import"), this);
        actions->addWidget(m_viewButton);
        actions->addStretch(1);
        actions->addWidget(m_importButton);
        layout->addLayout(actions);

        m_progress = new QProgressBar(this);
        m_progress->setVisible(false);
        layout->addWidget(m_progress);
        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        layout->addWidget(m_status);

        connect(searchButton, &QPushButton::clicked, this, [this] { Search(); });
        connect(m_query, &QLineEdit::returnPressed, this, [this] { Search(); });
        connect(m_moreButton, &QPushButton::clicked, this, [this] { Request(QUrl(m_next), false); });
        connect(m_results, &QListWidget::currentRowChanged, this, [this] { ShowSelection(); });
        connect(m_tokenButton, &QPushButton::clicked, this, [this] { EditToken(); });
        connect(m_viewButton, &QPushButton::clicked, this,
                [this] { QDesktopServices::openUrl(QUrl(Selected()["viewerUrl"].toString())); });
        connect(m_importButton, &QPushButton::clicked, this, [this] { Import(); });
        UpdateAccount();
        ShowSelection();
    }

private:
    QJsonObject Selected() const {
        const QListWidgetItem* item = m_results->currentItem();
        return item ? item->data(Qt::UserRole).toJsonObject() : QJsonObject{};
    }

    void UpdateAccount() {
        const bool hasToken = !LoadToken().isEmpty();
        m_accountLabel->setText(hasToken ? tr("Sketchfab API token saved in Windows Credential Manager.")
                                         : tr("Searching works without an account. Importing needs your "
                                              "Sketchfab API token."));
        m_tokenButton->setText(hasToken ? tr("Change token") : tr("Add API token"));
    }

    void EditToken() {
        QMessageBox box(QMessageBox::Question, tr("Sketchfab API token"),
                        tr("Copy your API token from the Password & API page of your Sketchfab settings, then "
                           "paste it here. It is kept in Windows Credential Manager, not in the project."),
                        QMessageBox::NoButton, this);
        QPushButton* open = box.addButton(tr("Open Sketchfab settings"), QMessageBox::ActionRole);
        QPushButton* paste = box.addButton(tr("Paste token"), QMessageBox::AcceptRole);
        QPushButton* remove = LoadToken().isEmpty() ? nullptr : box.addButton(tr("Remove token"),
                                                                              QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == open) {
            QDesktopServices::openUrl(QUrl(kTokenPage));
            return;
        }
        if (remove && box.clickedButton() == remove) {
            DeleteToken();
            UpdateAccount();
            return;
        }
        if (box.clickedButton() != paste) {
            return;
        }
        bool ok = false;
        const QString token = QInputDialog::getText(this, tr("Sketchfab API token"), tr("API token:"),
                                                    QLineEdit::Password, {}, &ok).trimmed();
        if (!ok || token.isEmpty()) {
            return;
        }
        QString error;
        if (!SaveToken(token, error)) {
            QMessageBox::warning(this, tr("Sketchfab"), error);
        }
        UpdateAccount();
    }

    void Search() { Request(SketchfabClient::SearchUrl(m_query->text().trimmed(), 24), true); }

    void Request(const QUrl& url, bool replace) {
        if (!m_client) {
            return;
        }
        if (replace) {
            m_results->clear();
        }
        m_moreButton->setVisible(false);
        m_status->setText(tr("Searching..."));
        QPointer<SketchfabPanel> self = this;
        m_client->Get(url, [self](bool ok, const QJsonObject& page, const QString& error) {
            if (!self) {
                return;
            }
            if (!ok) {
                self->m_status->setText(tr("Search failed: %1").arg(error));
                return;
            }
            for (const QJsonValue& value : page["results"].toArray()) {
                self->AddResult(value.toObject());
            }
            self->m_next = page["next"].toString();
            self->m_moreButton->setVisible(!self->m_next.isEmpty());
            self->m_status->setText(self->m_results->count() == 0 ? tr("No downloadable models found.") : QString());
        });
    }

    void AddResult(const QJsonObject& model) {
        const QString author = model["user"].toObject()["displayName"].toString();
        auto* item = new QListWidgetItem(QStringLiteral("%1\n%2").arg(model["name"].toString(), author), m_results);
        item->setData(Qt::UserRole, model);
        item->setToolTip(QStringLiteral("%1 by %2\n%3").arg(model["name"].toString(), author, LicenseLabel(model)));
        const QString thumbnail = ThumbnailUrl(model);
        if (thumbnail.isEmpty() || !m_client) {
            return;
        }
        QNetworkReply* reply = m_client->Network()->get(QNetworkRequest(QUrl(thumbnail)));
        QPointer<QListWidget> list = m_results;
        connect(reply, &QNetworkReply::finished, this, [reply, list, uid = model["uid"].toString()] {
            reply->deleteLater();
            QPixmap pixmap;
            if (!list || reply->error() != QNetworkReply::NoError || !pixmap.loadFromData(reply->readAll())) {
                return;
            }
            for (int i = 0; i < list->count(); ++i) {
                QListWidgetItem* entry = list->item(i);
                if (entry->data(Qt::UserRole).toJsonObject()["uid"].toString() == uid) {
                    entry->setIcon(QIcon(pixmap.scaled(160, 90, Qt::KeepAspectRatioByExpanding,
                                                       Qt::SmoothTransformation)));
                }
            }
        });
    }

    void ShowSelection() {
        const QJsonObject model = Selected();
        const bool has = !model.isEmpty();
        m_viewButton->setEnabled(has);
        m_importButton->setEnabled(has && !m_busy);
        if (!has) {
            m_details->setText(tr("Select a model to see its author and licence."));
            return;
        }
        const QJsonObject user = model["user"].toObject();
        const QString license = LicenseLabel(model);
        QString text = tr("<b>%1</b> by <a href=\"%2\">%3</a><br>Licence: <a href=\"%4\">%5</a>")
                           .arg(model["name"].toString().toHtmlEscaped(), user["profileUrl"].toString(),
                                user["displayName"].toString().toHtmlEscaped(), LicenseUrl(model),
                                license.toHtmlEscaped());
        if (IsNonCommercial(license)) {
            text += tr("<br><b>NonCommercial:</b> this model cannot be used in a game you sell.");
        }
        m_details->setText(text);
    }

    void SetBusy(bool busy, const QString& status) {
        m_busy = busy;
        m_progress->setVisible(busy);
        m_progress->setRange(0, 0);
        m_status->setText(status);
        ShowSelection();
    }

    void Import() {
        const QJsonObject model = Selected();
        if (model.isEmpty() || m_busy || !m_client) {
            return;
        }
        if (LoadToken().isEmpty()) {
            EditToken();
            if (LoadToken().isEmpty()) {
                return;
            }
        }
        const QString license = LicenseLabel(model);
        if (IsNonCommercial(license) &&
            QMessageBox::question(this, tr("NonCommercial licence"),
                                  tr("\"%1\" is licensed %2, which does not allow use in a game you sell. "
                                     "Import it anyway?").arg(model["name"].toString(), license)) !=
                QMessageBox::Yes) {
            return;
        }
        SetBusy(true, tr("Downloading %1...").arg(model["name"].toString()));
        QPointer<SketchfabPanel> self = this;
        m_client->Import(
            model,
            [self](qint64 received, qint64 total) {
                if (self && total > 0) {
                    self->m_progress->setRange(0, 1000);
                    self->m_progress->setValue(static_cast<int>(received * 1000 / total));
                }
            },
            [self](bool, const QString& summary, const AssetPackImportResult&) {
                if (self) {
                    self->SetBusy(false, summary);
                }
            });
    }

    QPointer<SketchfabClient> m_client;
    QLabel* m_accountLabel = nullptr;
    QPushButton* m_tokenButton = nullptr;
    QLineEdit* m_query = nullptr;
    QListWidget* m_results = nullptr;
    QPushButton* m_moreButton = nullptr;
    QLabel* m_details = nullptr;
    QPushButton* m_viewButton = nullptr;
    QPushButton* m_importButton = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_status = nullptr;
    QString m_next;
    bool m_busy = false;
};

QJsonObject Summary(const QJsonObject& model) {
    const QString license = LicenseLabel(model);
    return {{"uid", model["uid"]},
            {"name", model["name"]},
            {"author", model["user"].toObject()["displayName"]},
            {"license", license},
            {"licenseUrl", LicenseUrl(model)},
            {"nonCommercial", IsNonCommercial(license)},
            {"noDerivatives", IsNoDerivatives(license)},
            {"viewerUrl", model["viewerUrl"]},
            {"thumbnail", ThumbnailUrl(model)},
            {"faceCount", model["faceCount"]}};
}

class SketchfabPlugin final : public IHephaestusPlugin {
public:
    bool OnLoad(IPluginContext& context) override {
        m_context = &context;
        m_client = new SketchfabClient(context);
        bool ok = context.Commands().Register({"sketchfab.open", "Sketchfab",
                                               "Search Sketchfab and import downloadable models", "", &Open, this});
        ok &= context.UI().RegisterPanel({"sketchfab.browser", "Sketchfab", "", 2, &MakePanel, this});
        ok &= context.UI().RegisterMenuAction({"Window/Asset Stores", "sketchfab.open", "open"});
        if (IAutomationToolService* tools = context.AutomationTools()) {
            ok &= tools->RegisterTool(
                {"sketchfab.search",
                 "Search downloadable Sketchfab models. Returns each model's uid, name, author, licence (with "
                 "nonCommercial / noDerivatives flags), face count and page link, plus a `next` cursor URL for "
                 "more results. No account needed.",
                 R"({"type":"object","properties":{"query":{"type":"string"},)"
                 R"("count":{"type":"integer","minimum":1,"maximum":24},)"
                 R"("next":{"type":"string","description":"The next value from a previous search"}}})",
                 false, &SearchTool, this});
            ok &= tools->RegisterTool(
                {"sketchfab.import",
                 "Start downloading a Sketchfab model by uid and importing it into "
                 "assets/Imported/Sketchfab/<name> as a prefab with its materials applied and a credit for the "
                 "game's credits. Returns a job id at once; poll sketchfab.import_status. Uses the API token the "
                 "user saved in the Sketchfab panel. NonCommercial and NoDerivs models are refused unless "
                 "allowRestrictedLicense is true, which only the user should decide.",
                 R"({"type":"object","properties":{"uid":{"type":"string"},)"
                 R"("allowRestrictedLicense":{"type":"boolean"}},"required":["uid"]})",
                 true, &ImportTool, this});
            ok &= tools->RegisterTool(
                {"sketchfab.import_status",
                 "Progress and result of a sketchfab.import job: state (downloading, done, failed), progress 0-1, "
                 "and when done the folder, prefab and material counts and warnings.",
                 R"({"type":"object","properties":{"job":{"type":"integer"}},"required":["job"]})", false,
                 &StatusTool, this});
        }
        return ok;
    }

    void OnUnload() override {
        delete m_client;
        m_context = nullptr;
    }

private:
    struct Job {
        QJsonObject model;
        QString state = "downloading";
        double progress = 0.0;
        QJsonObject result;
        QString error;
    };

    static void Open(void* self) { static_cast<SketchfabPlugin*>(self)->m_context->UI().ShowPanel("sketchfab.browser"); }
    static QWidget* MakePanel(QWidget* parent, void* self) {
        return new SketchfabPanel(*static_cast<SketchfabPlugin*>(self)->m_client, parent);
    }

    void Answer(std::uint64_t call, bool ok, const QString& text) {
        if (m_context && m_context->AutomationTools()) {
            m_context->AutomationTools()->CompleteCall(call, ok, text.toUtf8().constData());
        }
    }

    static void SearchTool(const char* arguments, std::uint64_t call, void* data) {
        auto* self = static_cast<SketchfabPlugin*>(data);
        const QJsonObject args = QJsonDocument::fromJson(arguments).object();
        const QString next = args["next"].toString();
        if (!next.isEmpty() && !next.startsWith(QString(kApi) + "/search")) {
            self->Answer(call, false, "next must be a cursor URL returned by sketchfab.search");
            return;
        }
        const QUrl url = next.isEmpty()
            ? SketchfabClient::SearchUrl(args["query"].toString(), std::clamp(args["count"].toInt(12), 1, 24))
            : QUrl(next);
        self->m_client->Get(url, [self, call](bool ok, const QJsonObject& page, const QString& error) {
            if (!ok) {
                self->Answer(call, false, "Sketchfab search failed: " + error);
                return;
            }
            QJsonArray results;
            for (const QJsonValue& value : page["results"].toArray()) {
                results.append(Summary(value.toObject()));
            }
            self->Answer(call, true, Json({{"results", results}, {"next", page["next"].toString()}}));
        });
    }

    static void ImportTool(const char* arguments, std::uint64_t call, void* data) {
        auto* self = static_cast<SketchfabPlugin*>(data);
        const QJsonObject args = QJsonDocument::fromJson(arguments).object();
        const QString uid = args["uid"].toString();
        if (uid.isEmpty() || uid.contains('/') || uid.contains('?')) {
            self->Answer(call, false, "uid must be a Sketchfab model uid from sketchfab.search");
            return;
        }
        if (LoadToken().isEmpty()) {
            self->Answer(call, false, "No Sketchfab API token is saved. Ask the user to add one in Window > Asset "
                                      "Stores > Sketchfab.");
            return;
        }
        const bool allowRestricted = args["allowRestrictedLicense"].toBool(false);
        self->m_client->Get(QUrl(QString(kApi) + "/models/" + uid),
                            [self, call, allowRestricted](bool ok, const QJsonObject& model, const QString& error) {
            if (!ok) {
                self->Answer(call, false, "Could not read the model: " + error);
                return;
            }
            if (!model["isDownloadable"].toBool()) {
                self->Answer(call, false, "This model is not downloadable.");
                return;
            }
            const QString license = LicenseLabel(model);
            if ((IsNonCommercial(license) || IsNoDerivatives(license)) && !allowRestricted) {
                self->Answer(call, false, QStringLiteral("\"%1\" is licensed %2, which restricts commercial use "
                                                         "or changes. Ask the user before importing it with "
                                                         "allowRestrictedLicense.")
                                              .arg(model["name"].toString(), license));
                return;
            }
            const int job = self->m_nextJob++;
            self->m_jobs[job].model = model;
            self->m_client->Import(
                model,
                [self, job](qint64 received, qint64 total) {
                    if (total > 0) {
                        self->m_jobs[job].progress = static_cast<double>(received) / static_cast<double>(total);
                    }
                },
                [self, job](bool success, const QString& summary, const AssetPackImportResult& result) {
                    Job& entry = self->m_jobs[job];
                    entry.state = success ? "done" : "failed";
                    entry.progress = success ? 1.0 : entry.progress;
                    entry.error = success ? QString() : summary;
                    if (success) {
                        entry.result = {{"folder", QString::fromUtf8(result.folder)},
                                        {"prefabCount", static_cast<int>(result.prefabCount)},
                                        {"materialCount", static_cast<int>(result.materialCount)},
                                        {"warnings", QString::fromUtf8(result.message)},
                                        {"summary", summary}};
                    }
                });
            QJsonObject started = Summary(model);
            started["job"] = job;
            started["state"] = "downloading";
            self->Answer(call, true, Json(started));
        });
    }

    static void StatusTool(const char* arguments, std::uint64_t call, void* data) {
        auto* self = static_cast<SketchfabPlugin*>(data);
        const int job = QJsonDocument::fromJson(arguments).object()["job"].toInt(-1);
        const auto found = self->m_jobs.find(job);
        if (found == self->m_jobs.end()) {
            self->Answer(call, false, "Unknown sketchfab.import job");
            return;
        }
        QJsonObject status = {{"job", job},
                              {"name", found->second.model["name"]},
                              {"state", found->second.state},
                              {"progress", found->second.progress}};
        if (!found->second.result.isEmpty()) status["result"] = found->second.result;
        if (!found->second.error.isEmpty()) status["error"] = found->second.error;
        self->Answer(call, true, Json(status));
    }

    IPluginContext* m_context = nullptr;
    SketchfabClient* m_client = nullptr;
    std::map<int, Job> m_jobs;
    int m_nextJob = 1;
};

} // namespace

HP_DECLARE_PLUGIN(SketchfabPlugin)
