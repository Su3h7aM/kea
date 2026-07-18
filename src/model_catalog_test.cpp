/*
 * SPDX-FileCopyrightText: 2026 Kea contributors
 * SPDX-License-Identifier: MIT
 *
 * kea-model-catalog-test — parse/merge models.json + GGUF integrity checks.
 */
#include <cstdio>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "app/model_catalog.h"
#include "app/model_verify.h"

using namespace kea;

static int failures = 0;

static void check(bool cond, const char *msg)
{
    if (cond) {
        std::printf("  ok   %s\n", msg);
    } else {
        std::printf("  FAIL %s\n", msg);
        ++failures;
    }
}

static const char kSampleCatalog[] = R"json({
  "version": 1,
  "models": [
    {
      "id": "tdt-0.6b-v3",
      "name": "Parakeet TDT 0.6B v3",
      "description": "Offline multilingual",
      "streaming": false,
      "defaultQuant": "q8_0",
      "quants": [
        {
          "id": "f16",
          "label": "F16",
          "filename": "tdt-0.6b-v3-f16.gguf",
          "url": "https://example.test/tdt-0.6b-v3-f16.gguf",
          "sizeBytes": 100,
          "sizeHint": "~100 B",
          "recommended": true
        },
        {
          "id": "q8_0",
          "label": "Q8_0",
          "filename": "tdt-0.6b-v3-q8_0.gguf",
          "url": "https://example.test/tdt-0.6b-v3-q8_0.gguf",
          "sizeBytes": 50,
          "sizeHint": "~50 B"
        }
      ]
    },
    {
      "id": "realtime_eou_120m-v1",
      "name": "Realtime EOU",
      "description": "Streaming",
      "streaming": true,
      "defaultQuant": "q8_0",
      "quants": [
        {
          "id": "q8_0",
          "label": "Q8_0",
          "filename": "realtime_eou_120m-v1-q8_0.gguf",
          "url": "https://example.test/realtime_eou_120m-v1-q8_0.gguf",
          "sizeBytes": 40
        }
      ]
    }
  ]
})json";

static const char kUserOverride[] = R"json({
  "version": 1,
  "models": [
    {
      "id": "tdt-0.6b-v3",
      "name": "Custom TDT name",
      "streaming": false,
      "defaultQuant": "f16",
      "quants": [
        {
          "id": "q4_k",
          "label": "Q4_K",
          "filename": "tdt-0.6b-v3-q4_k.gguf",
          "url": "https://example.test/custom-q4.gguf",
          "sizeBytes": 25
        },
        {
          "id": "q8_0",
          "label": "Q8_0 custom",
          "filename": "custom-q8.gguf",
          "url": "https://example.test/custom-q8.gguf",
          "sizeBytes": 55
        }
      ]
    },
    {
      "id": "my-local-model",
      "name": "Homegrown",
      "streaming": false,
      "quants": [
        {
          "id": "f16",
          "label": "F16",
          "path": "/tmp/home.gguf"
        }
      ]
    }
  ]
})json";

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    std::printf("[catalog] parse valid v1 catalog\n");
    {
        QVector<ModelEntry> entries;
        QString err;
        check(ModelCatalog::parseCatalogJson(QByteArray(kSampleCatalog), &entries, &err),
              "parse ok");
        check(err.isEmpty(), "no error");
        check(entries.size() == 2, "2 models");
        check(entries[0].id == QStringLiteral("tdt-0.6b-v3"), "first id");
        check(entries[0].quants.size() == 2, "2 quants");
        check(entries[0].streaming == false, "offline flag");
        check(entries[1].streaming == true, "streaming flag");
        check(entries[0].quants[0].recommended == true, "f16 recommended");
        check(entries[0].quants[1].filename
                  == QStringLiteral("tdt-0.6b-v3-q8_0.gguf"),
              "q8 filename");
    }

    std::printf("[catalog] reject bad json\n");
    {
        QVector<ModelEntry> entries;
        QString err;
        check(!ModelCatalog::parseCatalogJson(QByteArrayLiteral("{not json"), &entries, &err),
              "parse fails");
        check(!err.isEmpty(), "error set");
    }

    std::printf("[catalog] reject missing quants\n");
    {
        QVector<ModelEntry> entries;
        QString err;
        const QByteArray bad = QByteArrayLiteral(
            R"({"models":[{"id":"x","name":"X","quants":[]}]})");
        check(!ModelCatalog::parseCatalogJson(bad, &entries, &err), "empty quants rejected");
    }

    std::printf("[catalog] merge user overrides\n");
    {
        QVector<ModelEntry> base;
        QVector<ModelEntry> user;
        QString err;
        check(ModelCatalog::parseCatalogJson(QByteArray(kSampleCatalog), &base, &err), "base");
        check(ModelCatalog::parseCatalogJson(QByteArray(kUserOverride), &user, &err), "user");
        const QVector<ModelEntry> merged = ModelCatalog::mergeCatalogs(base, user);
        check(merged.size() == 3, "3 models after merge (2 base + 1 new)");
        check(merged[0].name == QStringLiteral("Custom TDT name"), "name overridden");
        check(merged[0].defaultQuant == QStringLiteral("f16"), "defaultQuant overridden");
        // q8_0 replaced, q4_k added, f16 kept
        check(merged[0].quants.size() == 3, "3 quants after merge");
        bool foundCustomQ8 = false;
        bool foundQ4 = false;
        bool foundF16 = false;
        for (const ModelQuant &q : merged[0].quants) {
            if (q.id == QStringLiteral("q8_0")) {
                foundCustomQ8 = (q.filename == QStringLiteral("custom-q8.gguf"));
            }
            if (q.id == QStringLiteral("q4_k")) {
                foundQ4 = true;
            }
            if (q.id == QStringLiteral("f16")) {
                foundF16 = true;
            }
        }
        check(foundCustomQ8, "user q8_0 replaced");
        check(foundQ4, "user q4_k added");
        check(foundF16, "base f16 kept");
        check(merged[2].id == QStringLiteral("my-local-model"), "new user model appended");
        check(merged[2].quants[0].path == QStringLiteral("/tmp/home.gguf"), "local path kept");
        check(merged[2].quants[0].url.isEmpty(), "local-only has empty url");
        check(merged[2].quants[0].filename == QStringLiteral("home.gguf"),
              "filename derived from path");
    }

    std::printf("[catalog] path-only quant + availableSelections\n");
    {
        QTemporaryDir dir;
        check(dir.isValid(), "temp dir for local model");
        const QString modelPath = dir.filePath(QStringLiteral("mine.gguf"));
        {
            QFile f(modelPath);
            check(f.open(QIODevice::WriteOnly), "write local gguf");
            f.write("GGUF");
            f.write(QByteArray(2048, 'z'));
        }

        const QByteArray json = QByteArrayLiteral(
            "{\n"
            "  \"version\": 1,\n"
            "  \"models\": [{\n"
            "    \"id\": \"local-pack\",\n"
            "    \"name\": \"My folder\",\n"
            "    \"streaming\": false,\n"
            "    \"quants\": [{\n"
            "      \"id\": \"q8\",\n"
            "      \"label\": \"Q8\",\n"
            "      \"path\": \"")
            + modelPath.toUtf8() + QByteArrayLiteral("\"\n"
            "    }]\n"
            "  }]\n"
            "}");

        QVector<ModelEntry> entries;
        QString err;
        check(ModelCatalog::parseCatalogJson(json, &entries, &err), "parse path-only");
        check(entries.size() == 1, "1 model");
        check(entries[0].quants[0].url.isEmpty(), "no url");
        check(ModelCatalog::resolveQuantPath(entries[0].quants[0]) == modelPath,
              "resolveQuantPath");
        check(QFileInfo::exists(ModelCatalog::resolveQuantPath(entries[0].quants[0])),
              "file exists");

        // expand ~
        check(ModelCatalog::expandUserPath(QStringLiteral("~/foo.gguf"))
                  .startsWith(QDir::homePath()),
              "expand tilde");

        // reject neither url nor path
        QVector<ModelEntry> bad;
        check(!ModelCatalog::parseCatalogJson(
                  QByteArrayLiteral(
                      R"({"models":[{"id":"x","name":"X","quants":[{"id":"a","label":"A"}]}]})"),
                  &bad,
                  &err),
              "reject quant without url/path");
    }

    std::printf("[catalog] load bundled models.json if present\n");
    {
        const QString path = ModelCatalog::bundledCatalogPath();
        if (path.isEmpty()) {
            std::printf("  skip bundled path not found (ok in some CI layouts)\n");
        } else {
            QFile f(path);
            check(f.open(QIODevice::ReadOnly), "open bundled");
            QVector<ModelEntry> entries;
            QString err;
            check(ModelCatalog::parseCatalogJson(f.readAll(), &entries, &err), "parse bundled");
            check(entries.size() >= 2, "bundled has multiple models");
            bool hasTdt = false;
            bool hasStream = false;
            for (const ModelEntry &e : entries) {
                if (e.id == QStringLiteral("tdt-0.6b-v3")) {
                    hasTdt = e.quants.size() >= 2;
                }
                if (e.id == QStringLiteral("realtime_eou_120m-v1")) {
                    hasStream = e.streaming && !e.quants.isEmpty();
                }
            }
            check(hasTdt, "bundled has tdt-0.6b-v3 with quants");
            check(hasStream, "bundled has streaming EOU");
        }
    }

    std::printf("[verify] GGUF magic + size + sha256\n");
    {
        QTemporaryDir dir;
        check(dir.isValid(), "temp dir");
        const QString goodPath = dir.filePath(QStringLiteral("good.gguf"));
        {
            QFile f(goodPath);
            check(f.open(QIODevice::WriteOnly), "write good");
            f.write("GGUF");
            // Pad to above kGgufMinBytes
            QByteArray pad(2048, 'x');
            f.write(pad);
        }
        const qint64 goodSize = QFileInfo(goodPath).size();

        auto r = verifyModelFile(goodPath);
        check(r.ok, "magic-only ok");

        ModelIntegrityExpect exp;
        exp.sizeBytes = goodSize;
        r = verifyModelFile(goodPath, exp);
        check(r.ok, "size match ok");

        exp.sizeBytes = goodSize + 1;
        r = verifyModelFile(goodPath, exp);
        check(!r.ok, "size mismatch fails");
        check(r.error.contains(QStringLiteral("size")), "size error message");

        // Wrong magic
        const QString badPath = dir.filePath(QStringLiteral("bad.bin"));
        {
            QFile f(badPath);
            f.open(QIODevice::WriteOnly);
            f.write("HTTP");
            f.write(QByteArray(2048, 'y'));
        }
        r = verifyModelFile(badPath);
        check(!r.ok, "bad magic fails");
        check(r.error.contains(QStringLiteral("GGUF"), Qt::CaseInsensitive), "magic error");

        // Too small
        const QString tinyPath = dir.filePath(QStringLiteral("tiny.gguf"));
        {
            QFile f(tinyPath);
            f.open(QIODevice::WriteOnly);
            f.write("GGUF");
        }
        r = verifyModelFile(tinyPath);
        check(!r.ok, "too small fails");

        // SHA-256
        QFile gf(goodPath);
        gf.open(QIODevice::ReadOnly);
        const QByteArray all = gf.readAll();
        const QString digest = QString::fromLatin1(
            QCryptographicHash::hash(all, QCryptographicHash::Sha256).toHex());
        exp = ModelIntegrityExpect{};
        exp.sha256 = digest;
        r = verifyModelFile(goodPath, exp);
        check(r.ok, "sha256 match ok");

        exp.sha256 = QStringLiteral("deadbeef");
        r = verifyModelFile(goodPath, exp);
        check(!r.ok, "sha256 mismatch fails");
    }

    if (failures == 0) {
        std::printf("All model-catalog tests passed.\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
