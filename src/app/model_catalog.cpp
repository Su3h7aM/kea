/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 */
#include "model_catalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStandardPaths>
#include <QVariant>

#include "app_settings.h"
#include "logging.h"

#ifndef KEA_MODELS_JSON
#define KEA_MODELS_JSON ""
#endif

namespace kea {

namespace {

QByteArray readFileBytes(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return f.readAll();
}

} // namespace

ModelCatalog::ModelCatalog(QObject *parent)
    : QObject(parent)
{
    reload();
}

QString ModelCatalog::userCatalogPath() const
{
    return defaultUserCatalogPath();
}

QString ModelCatalog::defaultUserCatalogPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/kea/models.json");
}

QString ModelCatalog::bundledCatalogPath()
{
    // 1) Compile-time path (dev builds point at the source tree).
    const QString compileTime = QString::fromUtf8(KEA_MODELS_JSON);
    if (!compileTime.isEmpty() && QFileInfo::exists(compileTime)) {
        return compileTime;
    }

    // 2) Installed XDG data location.
    const QString located = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                   QStringLiteral("kea/models.json"));
    if (!located.isEmpty()) {
        return located;
    }

    // 3) Relative to the running binary (prefix-style install).
    const QString besideApp = QDir(QCoreApplication::applicationDirPath())
                                  .absoluteFilePath(QStringLiteral("../share/kea/models.json"));
    if (QFileInfo::exists(besideApp)) {
        return QFileInfo(besideApp).canonicalFilePath();
    }

    return {};
}

QString ModelCatalog::defaultModelId() const
{
    return QStringLiteral("tdt-0.6b-v3");
}

QString ModelCatalog::defaultQuantId() const
{
    return QStringLiteral("q8_0");
}

QString ModelCatalog::expandUserPath(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    if (trimmed == QLatin1String("~")) {
        return QDir::homePath();
    }
    if (trimmed.startsWith(QLatin1String("~/"))) {
        return QDir::homePath() + trimmed.mid(1);
    }
    return trimmed;
}

QString ModelCatalog::resolveQuantPath(const ModelQuant &q)
{
    if (!q.path.isEmpty()) {
        return expandUserPath(q.path);
    }
    if (q.filename.isEmpty()) {
        return {};
    }
    return AppSettings::defaultModelsDir() + QLatin1Char('/') + q.filename;
}

void ModelCatalog::setError(const QString &e)
{
    if (m_lastError == e) {
        return;
    }
    m_lastError = e;
    Q_EMIT lastErrorChanged();
}

void ModelCatalog::reload()
{
    QVector<ModelEntry> base;
    QString err;

    const QString bundled = bundledCatalogPath();
    if (bundled.isEmpty()) {
        setError(QStringLiteral("bundled models.json not found"));
        qCWarning(keaLog) << "ModelCatalog: no bundled models.json";
    } else {
        const QByteArray bytes = readFileBytes(bundled);
        if (bytes.isEmpty()) {
            setError(QStringLiteral("cannot read bundled models.json: %1").arg(bundled));
            qCWarning(keaLog) << "ModelCatalog: cannot read" << bundled;
        } else if (!parseCatalogJson(bytes, &base, &err)) {
            setError(QStringLiteral("bundled models.json: %1").arg(err));
            qCWarning(keaLog) << "ModelCatalog: parse failed for" << bundled << err;
            base.clear();
        } else {
            qCInfo(keaLog) << "ModelCatalog: loaded" << base.size() << "models from" << bundled;
        }
    }

    QVector<ModelEntry> user;
    const QString userPath = defaultUserCatalogPath();
    if (QFileInfo::exists(userPath)) {
        const QByteArray bytes = readFileBytes(userPath);
        if (bytes.isEmpty()) {
            qCWarning(keaLog) << "ModelCatalog: cannot read user catalog" << userPath;
        } else if (!parseCatalogJson(bytes, &user, &err)) {
            // Keep bundled entries; surface the user parse error.
            setError(QStringLiteral("user models.json: %1").arg(err));
            qCWarning(keaLog) << "ModelCatalog: user catalog parse failed:" << err;
            user.clear();
        } else {
            qCInfo(keaLog) << "ModelCatalog: merged" << user.size()
                           << "user model entries from" << userPath;
        }
    }

    m_entries = mergeCatalogs(base, user);
    if (!m_entries.isEmpty() && m_lastError.startsWith(QStringLiteral("bundled"))) {
        setError(QString());
    } else if (!m_entries.isEmpty() && !m_lastError.startsWith(QStringLiteral("user"))) {
        setError(QString());
    }

    rebuildVariantList();
    Q_EMIT modelsChanged();
    Q_EMIT availabilityChanged();
}

void ModelCatalog::refreshAvailability()
{
    Q_EMIT availabilityChanged();
}

void ModelCatalog::rebuildVariantList()
{
    m_modelsVariant.clear();
    m_modelsVariant.reserve(m_entries.size());
    for (const ModelEntry &e : m_entries) {
        m_modelsVariant.append(modelToVariant(e));
    }
}

QVariantMap ModelCatalog::modelToVariant(const ModelEntry &e)
{
    QVariantMap m;
    m.insert(QStringLiteral("id"), e.id);
    m.insert(QStringLiteral("name"), e.name);
    m.insert(QStringLiteral("description"), e.description);
    m.insert(QStringLiteral("streaming"), e.streaming);
    m.insert(QStringLiteral("source"), e.source);
    m.insert(QStringLiteral("defaultQuant"), e.defaultQuant);
    m.insert(QStringLiteral("quantCount"), e.quants.size());
    QString mode = e.streaming ? QStringLiteral("Streaming") : QStringLiteral("Offline");
    m.insert(QStringLiteral("subtitle"),
             QStringLiteral("%1 · %2 quantizations").arg(mode).arg(e.quants.size()));
    return m;
}

QVariantMap ModelCatalog::quantToVariant(const ModelQuant &q) const
{
    QVariantMap m;
    m.insert(QStringLiteral("id"), q.id);
    m.insert(QStringLiteral("label"), q.label);
    m.insert(QStringLiteral("filename"), q.filename);
    m.insert(QStringLiteral("url"), q.url);
    m.insert(QStringLiteral("path"), q.path);
    m.insert(QStringLiteral("sizeBytes"), q.sizeBytes);
    m.insert(QStringLiteral("sizeHint"), q.sizeHint);
    m.insert(QStringLiteral("sha256"), q.sha256);
    m.insert(QStringLiteral("recommended"), q.recommended);
    m.insert(QStringLiteral("downloadable"), !q.url.isEmpty());
    const QString resolved = resolveQuantPath(q);
    m.insert(QStringLiteral("resolvedPath"), resolved);
    m.insert(QStringLiteral("available"),
             !resolved.isEmpty() && QFileInfo::exists(resolved) && QFileInfo(resolved).isFile());

    QString display = q.label;
    if (!q.sizeHint.isEmpty()) {
        display += QStringLiteral(" (") + q.sizeHint + QStringLiteral(")");
    }
    if (!q.path.isEmpty()) {
        display += QStringLiteral(" · local");
    }
    if (q.recommended) {
        display += QStringLiteral(" ★");
    }
    m.insert(QStringLiteral("display"), display);
    return m;
}

QVariantMap ModelCatalog::selectionToVariant(const ModelEntry &e, const ModelQuant &q) const
{
    QVariantMap m;
    const QString path = resolveQuantPath(q);
    m.insert(QStringLiteral("key"),
             QString(e.id + QLatin1Char('/') + q.id));
    m.insert(QStringLiteral("modelId"), e.id);
    m.insert(QStringLiteral("quantId"), q.id);
    m.insert(QStringLiteral("path"), path);
    m.insert(QStringLiteral("streaming"), e.streaming);
    m.insert(QStringLiteral("hasExplicitPath"), !q.path.isEmpty());
    // "Parakeet TDT 0.6B v3 · Q8_0" (+ local / streaming hints)
    QString display = e.name + QStringLiteral(" · ") + q.label;
    if (e.streaming) {
        display += QStringLiteral(" (streaming)");
    }
    if (!q.path.isEmpty()) {
        display += QStringLiteral(" · local");
    }
    m.insert(QStringLiteral("display"), display);
    m.insert(QStringLiteral("description"), e.description);
    return m;
}

const ModelEntry *ModelCatalog::findModel(const QString &modelId) const
{
    for (const ModelEntry &e : m_entries) {
        if (e.id == modelId) {
            return &e;
        }
    }
    return nullptr;
}

const ModelQuant *ModelCatalog::findQuant(const QString &modelId, const QString &quantId) const
{
    const ModelEntry *e = findModel(modelId);
    if (!e) {
        return nullptr;
    }
    for (const ModelQuant &q : e->quants) {
        if (q.id == quantId) {
            return &q;
        }
    }
    return nullptr;
}

QVariantList ModelCatalog::quantsFor(const QString &modelId) const
{
    QVariantList out;
    const ModelEntry *e = findModel(modelId);
    if (!e) {
        return out;
    }
    for (const ModelQuant &q : e->quants) {
        out.append(quantToVariant(q));
    }
    return out;
}

QVariantMap ModelCatalog::quant(const QString &modelId, const QString &quantId) const
{
    const ModelQuant *q = findQuant(modelId, quantId);
    if (!q) {
        return {};
    }
    return quantToVariant(*q);
}

QVariantMap ModelCatalog::preferredQuant(const QString &modelId) const
{
    const ModelEntry *e = findModel(modelId);
    if (!e || e->quants.isEmpty()) {
        return {};
    }
    if (!e->defaultQuant.isEmpty()) {
        for (const ModelQuant &q : e->quants) {
            if (q.id == e->defaultQuant) {
                return quantToVariant(q);
            }
        }
    }
    for (const ModelQuant &q : e->quants) {
        if (q.recommended) {
            return quantToVariant(q);
        }
    }
    return quantToVariant(e->quants.first());
}

QString ModelCatalog::resolvedPath(const QString &modelId, const QString &quantId) const
{
    const ModelQuant *q = findQuant(modelId, quantId);
    if (!q) {
        return {};
    }
    return resolveQuantPath(*q);
}

bool ModelCatalog::isAvailable(const QString &modelId, const QString &quantId) const
{
    const QString path = resolvedPath(modelId, quantId);
    return !path.isEmpty() && QFileInfo::exists(path) && QFileInfo(path).isFile();
}

bool ModelCatalog::isDownloadable(const QString &modelId, const QString &quantId) const
{
    const ModelQuant *q = findQuant(modelId, quantId);
    return q && !q->url.isEmpty();
}

QString ModelCatalog::downloadDest(const QString &modelId, const QString &quantId) const
{
    const ModelQuant *q = findQuant(modelId, quantId);
    if (!q) {
        return {};
    }
    // Downloads always land in the Kea models dir under the catalog filename,
    // even when a local `path` override exists (path is for selection only).
    if (q->filename.isEmpty()) {
        return {};
    }
    return AppSettings::defaultModelsDir() + QLatin1Char('/') + q->filename;
}

QVariantList ModelCatalog::availableSelections() const
{
    QVariantList out;
    for (const ModelEntry &e : m_entries) {
        for (const ModelQuant &q : e.quants) {
            const QString path = resolveQuantPath(q);
            if (path.isEmpty() || !QFileInfo::exists(path) || !QFileInfo(path).isFile()) {
                continue;
            }
            out.append(selectionToVariant(e, q));
        }
    }
    return out;
}

int ModelCatalog::indexOfAvailablePath(const QString &path) const
{
    if (path.isEmpty()) {
        return -1;
    }
    const QString want = QFileInfo(expandUserPath(path)).absoluteFilePath();
    const QVariantList list = availableSelections();
    for (int i = 0; i < list.size(); ++i) {
        const QVariantMap m = list.at(i).toMap();
        const QString p = QFileInfo(m.value(QStringLiteral("path")).toString()).absoluteFilePath();
        if (p == want) {
            return i;
        }
    }
    return -1;
}

bool ModelCatalog::parseCatalogJson(const QByteArray &json,
                                    QVector<ModelEntry> *out,
                                    QString *error)
{
    if (!out) {
        if (error) {
            *error = QStringLiteral("null out");
        }
        return false;
    }
    out->clear();

    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError) {
        if (error) {
            *error = pe.errorString();
        }
        return false;
    }
    if (!doc.isObject()) {
        if (error) {
            *error = QStringLiteral("root must be an object");
        }
        return false;
    }

    const QJsonObject root = doc.object();
    const QJsonValue modelsVal = root.value(QStringLiteral("models"));
    if (!modelsVal.isArray()) {
        if (error) {
            *error = QStringLiteral("missing models array");
        }
        return false;
    }

    const QJsonArray arr = modelsVal.toArray();
    out->reserve(arr.size());
    for (const QJsonValue &v : arr) {
        if (!v.isObject()) {
            if (error) {
                *error = QStringLiteral("model entry must be an object");
            }
            return false;
        }
        QString entryErr;
        const ModelEntry entry = parseModelObject(v.toObject().toVariantMap(), &entryErr);
        if (!entryErr.isEmpty()) {
            if (error) {
                *error = entryErr;
            }
            return false;
        }
        if (entry.id.isEmpty() || entry.quants.isEmpty()) {
            if (error) {
                *error = QStringLiteral("model requires id and at least one quant");
            }
            return false;
        }
        out->append(entry);
    }
    return true;
}

ModelEntry ModelCatalog::parseModelObject(const QVariantMap &m, QString *error)
{
    ModelEntry e;
    e.id = m.value(QStringLiteral("id")).toString().trimmed();
    e.name = m.value(QStringLiteral("name")).toString().trimmed();
    e.description = m.value(QStringLiteral("description")).toString().trimmed();
    e.streaming = m.value(QStringLiteral("streaming")).toBool();
    e.source = m.value(QStringLiteral("source")).toString().trimmed();
    e.defaultQuant = m.value(QStringLiteral("defaultQuant")).toString().trimmed();

    if (e.id.isEmpty()) {
        if (error) {
            *error = QStringLiteral("model missing id");
        }
        return e;
    }
    if (e.name.isEmpty()) {
        e.name = e.id;
    }

    const QVariant quantsVar = m.value(QStringLiteral("quants"));
    if (!quantsVar.canConvert<QVariantList>()) {
        if (error) {
            *error = QStringLiteral("model %1: missing quants array").arg(e.id);
        }
        return e;
    }
    const QVariantList qlist = quantsVar.toList();
    for (const QVariant &qv : qlist) {
        QString qerr;
        const ModelQuant q = parseQuantObject(qv.toMap(), &qerr);
        if (!qerr.isEmpty()) {
            if (error) {
                *error = QStringLiteral("model %1: %2").arg(e.id, qerr);
            }
            e.quants.clear();
            return e;
        }
        e.quants.append(q);
    }
    return e;
}

ModelQuant ModelCatalog::parseQuantObject(const QVariantMap &q, QString *error)
{
    ModelQuant out;
    out.id = q.value(QStringLiteral("id")).toString().trimmed();
    out.label = q.value(QStringLiteral("label")).toString().trimmed();
    out.filename = q.value(QStringLiteral("filename")).toString().trimmed();
    out.url = q.value(QStringLiteral("url")).toString().trimmed();
    out.path = q.value(QStringLiteral("path")).toString().trimmed();
    out.sizeBytes = q.value(QStringLiteral("sizeBytes")).toLongLong();
    out.sizeHint = q.value(QStringLiteral("sizeHint")).toString().trimmed();
    out.sha256 = q.value(QStringLiteral("sha256")).toString().trimmed();
    out.recommended = q.value(QStringLiteral("recommended")).toBool();

    if (out.id.isEmpty()) {
        if (error) {
            *error = QStringLiteral("quant missing id");
        }
        return out;
    }
    if (out.label.isEmpty()) {
        out.label = out.id.toUpper();
    }

    // Local-only entries: path without url is valid. Downloadable: url required.
    if (out.path.isEmpty() && out.url.isEmpty()) {
        if (error) {
            *error = QStringLiteral("quant %1 needs url and/or path").arg(out.id);
        }
        return out;
    }

    if (out.filename.isEmpty()) {
        if (!out.path.isEmpty()) {
            out.filename = QFileInfo(expandUserPath(out.path)).fileName();
        } else if (!out.url.isEmpty()) {
            // Last path segment of the URL (strip query).
            QString last = out.url;
            const int qpos = last.indexOf(QLatin1Char('?'));
            if (qpos >= 0) {
                last = last.left(qpos);
            }
            const int slash = last.lastIndexOf(QLatin1Char('/'));
            if (slash >= 0) {
                last = last.mid(slash + 1);
            }
            out.filename = last;
        }
    }
    if (out.filename.isEmpty()) {
        if (error) {
            *error = QStringLiteral("quant %1 missing filename").arg(out.id);
        }
        return out;
    }

    if (out.sizeHint.isEmpty() && out.sizeBytes > 0) {
        const double mb = static_cast<double>(out.sizeBytes) / (1024.0 * 1024.0);
        if (mb >= 1024.0) {
            out.sizeHint = QStringLiteral("~%1 GB").arg(mb / 1024.0, 0, 'f', 1);
        } else {
            out.sizeHint = QStringLiteral("~%1 MB").arg(int(mb + 0.5));
        }
    }
    return out;
}

QVector<ModelEntry> ModelCatalog::mergeCatalogs(const QVector<ModelEntry> &base,
                                                const QVector<ModelEntry> &user)
{
    if (user.isEmpty()) {
        return base;
    }
    if (base.isEmpty()) {
        return user;
    }

    QVector<ModelEntry> result = base;
    QHash<QString, int> indexById;
    indexById.reserve(result.size());
    for (int i = 0; i < result.size(); ++i) {
        indexById.insert(result[i].id, i);
    }

    for (const ModelEntry &u : user) {
        const auto it = indexById.constFind(u.id);
        if (it == indexById.cend()) {
            indexById.insert(u.id, result.size());
            result.append(u);
            continue;
        }
        ModelEntry &dst = result[*it];
        if (!u.name.isEmpty()) {
            dst.name = u.name;
        }
        if (!u.description.isEmpty()) {
            dst.description = u.description;
        }
        dst.streaming = u.streaming;
        if (!u.source.isEmpty()) {
            dst.source = u.source;
        }
        if (!u.defaultQuant.isEmpty()) {
            dst.defaultQuant = u.defaultQuant;
        }
        QHash<QString, int> qIndex;
        for (int qi = 0; qi < dst.quants.size(); ++qi) {
            qIndex.insert(dst.quants[qi].id, qi);
        }
        for (const ModelQuant &uq : u.quants) {
            const auto qit = qIndex.constFind(uq.id);
            if (qit == qIndex.cend()) {
                qIndex.insert(uq.id, dst.quants.size());
                dst.quants.append(uq);
            } else {
                dst.quants[*qit] = uq;
            }
        }
    }
    return result;
}

} // namespace kea
