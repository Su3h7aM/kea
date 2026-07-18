/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * ModelCatalog — loads the bundled models.json and optional user overrides
 * from ~/.config/kea/models.json, merges them, and exposes the result to QML.
 *
 * Schema v1: each catalog entry is a *model* with nested *quantizations*:
 *   { version, models: [ { id, name, description, streaming, defaultQuant,
 *       quants: [ { id, label, filename?, url?, path?, sizeBytes, sizeHint,
 *                   sha256?, recommended } ] } ] }
 *
 * A quant is downloadable when it has a non-empty `url`. It may also declare
 * an absolute (or ~/…) `path` for a file the user already has on disk. At
 * least one of `url` or `path` is required. `filename` defaults to the path
 * basename when only `path` is set.
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
    QString filename; ///< used for default download dest name
    QString url;      ///< empty = not downloadable (local-only entry)
    QString path;     ///< optional absolute or ~/ path to an existing file
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
    /// Quant variants that resolve to an existing file (for the Active model picker).
    Q_PROPERTY(QVariantList availableSelections READ availableSelections NOTIFY availabilityChanged)
    /// Path of the user override file (~/.config/kea/models.json).
    Q_PROPERTY(QString userCatalogPath READ userCatalogPath CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    /// ASR catalog (data/models.json → ~/.local/share/kea/models).
    explicit ModelCatalog(QObject *parent = nullptr);

    /// Custom catalog roots (LLM post-process models, etc.).
    ModelCatalog(const QString &compileTimeBundledPath,
                 const QString &installRelativePath,
                 const QString &userFileName,
                 const QString &modelsDirName,
                 const QString &defaultModelId,
                 const QString &defaultQuantId,
                 QObject *parent = nullptr);

    /// Factory for the LLM post-processing catalog (LFM, …).
    static ModelCatalog *createLlmCatalog(QObject *parent = nullptr);

    QVariantList models() const { return m_modelsVariant; }
    int count() const { return m_entries.size(); }
    QVariantList availableSelections() const;
    QString userCatalogPath() const { return m_userCatalogPath; }
    QString modelsDirectory() const { return m_modelsDir; }
    QString lastError() const { return m_lastError; }

    const QVector<ModelEntry> &entries() const { return m_entries; }

    /// Quant list for a model id (maps suitable for QML ComboBox).
    Q_INVOKABLE QVariantList quantsFor(const QString &modelId) const;

    /// Single quant map, or empty map if not found.
    Q_INVOKABLE QVariantMap quant(const QString &modelId, const QString &quantId) const;

    /// First quant marked recommended, else defaultQuant, else first quant.
    Q_INVOKABLE QVariantMap preferredQuant(const QString &modelId) const;

    /// Resolved filesystem path for a catalog quant (explicit path, else modelsDir/filename).
    Q_INVOKABLE QString resolvedPath(const QString &modelId, const QString &quantId) const;

    /// True when the resolved path exists on disk.
    Q_INVOKABLE bool isAvailable(const QString &modelId, const QString &quantId) const;

    /// True when the quant has a download URL.
    Q_INVOKABLE bool isDownloadable(const QString &modelId, const QString &quantId) const;

    /// Default destination for a download of this quant (modelsDir/filename).
    Q_INVOKABLE QString downloadDest(const QString &modelId, const QString &quantId) const;

    /// Index into availableSelections() for `path`, or -1.
    Q_INVOKABLE int indexOfAvailablePath(const QString &path) const;

    /// Default offline model id used by AppSettings::defaultModelPath.
    Q_INVOKABLE QString defaultModelId() const;
    Q_INVOKABLE QString defaultQuantId() const;

    /// Reload bundled + user catalogs from disk.
    Q_INVOKABLE void reload();

    /// Re-scan disk for available selections (after download / external copy).
    Q_INVOKABLE void refreshAvailability();

    /// Expand ~/ and clean a configured path (public for tests).
    static QString expandUserPath(const QString &path);

    /// Parse JSON bytes into entries (public for unit tests). Does not merge.
    static bool parseCatalogJson(const QByteArray &json,
                                 QVector<ModelEntry> *out,
                                 QString *error);

    /// Merge user entries into base (same model id → user fields win; quants
    /// merge by quant id with user winning; unknown models append).
    static QVector<ModelEntry> mergeCatalogs(const QVector<ModelEntry> &base,
                                             const QVector<ModelEntry> &user);

    /// Locate a bundled catalog (compile-time path, then install relative).
    static QString locateBundledCatalog(const QString &compileTimePath,
                                        const QString &installRelative);

    static QString defaultUserCatalogPath();
    static QString defaultLlmUserCatalogPath();
    static QString defaultLlmModelsDir();

    /// Resolve where a quant lives on disk (explicit path wins).
    QString resolveQuantPath(const ModelQuant &q) const;
    static QString resolveQuantPath(const ModelQuant &q, const QString &modelsDir);

Q_SIGNALS:
    void modelsChanged();
    void availabilityChanged();
    void lastErrorChanged();

private:
    void setError(const QString &e);
    void rebuildVariantList();
    static ModelEntry parseModelObject(const QVariantMap &m, QString *error);
    static ModelQuant parseQuantObject(const QVariantMap &q, QString *error);
    static QVariantMap modelToVariant(const ModelEntry &e);
    QVariantMap quantToVariant(const ModelQuant &q) const;
    QVariantMap selectionToVariant(const ModelEntry &e, const ModelQuant &q) const;
    const ModelQuant *findQuant(const QString &modelId, const QString &quantId) const;
    const ModelEntry *findModel(const QString &modelId) const;

    QVector<ModelEntry> m_entries;
    QVariantList m_modelsVariant;
    QString m_lastError;
    QString m_bundledCompileTime;
    QString m_installRelative;
    QString m_userCatalogPath;
    QString m_modelsDir;
    QString m_defaultModelId;
    QString m_defaultQuantId;
};

} // namespace kea
