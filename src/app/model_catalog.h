/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * ModelCatalog — loads the bundled models.json and optional user overrides
 * from ~/.config/kea/models.json, merges them, and exposes the result to QML.
 *
 * Schema v1: each catalog entry is a *model* with nested *quantizations*:
 *   { version, models: [ { id, name, description, streaming, defaultQuant,
 *       quants: [ { id, label, filename, url, sizeBytes, sizeHint, sha256?,
 *                   recommended } ] } ] }
 */
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace kea {

struct ModelQuant {
    QString id;       ///< e.g. "q8_0"
    QString label;    ///< e.g. "Q8_0"
    QString filename;
    QString url;
    qint64 sizeBytes = 0;
    QString sizeHint;
    QString sha256;   ///< optional hex digest
    bool recommended = false;
};

struct ModelEntry {
    QString id;
    QString name;
    QString description;
    bool streaming = false;
    QString source;
    QString defaultQuant;
    QVector<ModelQuant> quants;
};

class ModelCatalog : public QObject
{
    Q_OBJECT
    /// Flat list of model maps for QML ComboBox / ListView (no nested objects).
    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged)
    Q_PROPERTY(int count READ count NOTIFY modelsChanged)
    /// Path of the user override file (~/.config/kea/models.json).
    Q_PROPERTY(QString userCatalogPath READ userCatalogPath CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit ModelCatalog(QObject *parent = nullptr);

    QVariantList models() const { return m_modelsVariant; }
    int count() const { return m_entries.size(); }
    QString userCatalogPath() const;
    QString lastError() const { return m_lastError; }

    const QVector<ModelEntry> &entries() const { return m_entries; }

    /// Quant list for a model id (maps suitable for QML ComboBox).
    Q_INVOKABLE QVariantList quantsFor(const QString &modelId) const;

    /// Single quant map, or empty map if not found.
    Q_INVOKABLE QVariantMap quant(const QString &modelId, const QString &quantId) const;

    /// First quant marked recommended, else defaultQuant, else first quant.
    Q_INVOKABLE QVariantMap preferredQuant(const QString &modelId) const;

    /// Absolute path where a download of `filename` would land.
    Q_INVOKABLE QString localPathFor(const QString &filename) const;

    /// True when modelsDir()/filename already exists on disk.
    Q_INVOKABLE bool isInstalled(const QString &filename) const;

    /// Default offline model id used by AppSettings::defaultModelPath.
    Q_INVOKABLE QString defaultModelId() const;
    Q_INVOKABLE QString defaultQuantId() const;

    /// Reload bundled + user catalogs from disk.
    Q_INVOKABLE void reload();

    /// Parse JSON bytes into entries (public for unit tests). Does not merge.
    static bool parseCatalogJson(const QByteArray &json,
                                 QVector<ModelEntry> *out,
                                 QString *error);

    /// Merge user entries into base (same model id → user fields win; quants
    /// merge by quant id with user winning; unknown models append).
    static QVector<ModelEntry> mergeCatalogs(const QVector<ModelEntry> &base,
                                             const QVector<ModelEntry> &user);

    /// Locate the bundled catalog file (install path, then build-time path).
    static QString bundledCatalogPath();

    static QString defaultUserCatalogPath();

Q_SIGNALS:
    void modelsChanged();
    void lastErrorChanged();

private:
    void setError(const QString &e);
    void rebuildVariantList();
    static ModelEntry parseModelObject(const QVariantMap &m, QString *error);
    static ModelQuant parseQuantObject(const QVariantMap &q, QString *error);
    static QVariantMap modelToVariant(const ModelEntry &e);
    static QVariantMap quantToVariant(const ModelQuant &q);

    QVector<ModelEntry> m_entries;
    QVariantList m_modelsVariant;
    QString m_lastError;
};

} // namespace kea
