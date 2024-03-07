/*
 * Copyright 2024 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/gpu/graphite/SmallPathAtlas.h"

#include "include/gpu/graphite/Recorder.h"
#include "src/gpu/graphite/RasterPathUtils.h"
#include "src/gpu/graphite/RecorderPriv.h"
#include "src/gpu/graphite/TextureProxy.h"

namespace skgpu::graphite {

static constexpr size_t kPlotWidth = 512;
static constexpr size_t kPlotHeight = 256;

SmallPathAtlas::SmallPathAtlas(Recorder* recorder)
        : PathAtlas(recorder, kDefaultAtlasDim, kDefaultAtlasDim)
        , fAtlasMgr(fWidth, fHeight, kPlotWidth, kPlotHeight) {
    SkASSERT(fRecorder);
}

bool SmallPathAtlas::recordUploads(DrawContext* dc) {
    return fAtlasMgr.recordUploads(dc, fRecorder);
}

const TextureProxy* SmallPathAtlas::onAddShape(const Shape& shape,
                                               const Transform& transform,
                                               const SkStrokeRec& strokeRec,
                                               skvx::half2 maskSize,
                                               skvx::half2* outPos) {
    return fAtlasMgr.findOrCreateEntry(fRecorder, shape, transform, strokeRec, maskSize, outPos);
}

/////////////////////////////////////////////////////////////////////////////////////////

SmallPathAtlas::DrawAtlasMgr::DrawAtlasMgr(size_t width, size_t height,
                                           size_t plotWidth, size_t plotHeight) {
    static constexpr SkColorType colorType = kAlpha_8_SkColorType;

    fDrawAtlas = DrawAtlas::Make(colorType,
                                 SkColorTypeBytesPerPixel(colorType),
                                 width, height,
                                 plotWidth, plotHeight,
                                 this,
                                 DrawAtlas::AllowMultitexturing::kYes,
                                 this,
                                 /*label=*/"SmallPathAtlas");
    SkASSERT(fDrawAtlas);
    fKeyLists.resize(fDrawAtlas->numPlots() * fDrawAtlas->maxPages());
    for (int i = 0; i < fKeyLists.size(); ++i) {
        fKeyLists[i].reset();
    }
}

namespace {
uint32_t shape_key_list_index(const PlotLocator& locator, const DrawAtlas* drawAtlas) {
    return locator.pageIndex() * drawAtlas->numPlots() + locator.plotIndex();
}
}  // namespace

const TextureProxy* SmallPathAtlas::DrawAtlasMgr::findOrCreateEntry(Recorder* recorder,
                                                                    const Shape& shape,
                                                                    const Transform& transform,
                                                                    const SkStrokeRec& strokeRec,
                                                                    skvx::half2 maskSize,
                                                                    skvx::half2* outPos) {
    // Shapes must have a key to be stored in the SmallPathAtlas
    SkASSERT(shape.hasKey());
    skgpu::UniqueKey maskKey = GeneratePathMaskKey(shape, transform, strokeRec, maskSize);
    AtlasLocator* cachedLocator = fShapeCache.find(maskKey);
    if (cachedLocator) {
        SkIPoint topLeft = cachedLocator->topLeft();
        *outPos = skvx::half2(topLeft.x() + kEntryPadding, topLeft.y() + kEntryPadding);
        fDrawAtlas->setLastUseToken(*cachedLocator,
                                    recorder->priv().tokenTracker()->nextFlushToken());
        return fDrawAtlas->getProxies()[cachedLocator->pageIndex()].get();
    }

    // Render mask.
    SkIRect iShapeBounds = SkIRect::MakeXYWH(0, 0, maskSize.x(), maskSize.y());
    // Outset to take padding into account
    SkIRect iAtlasBounds = iShapeBounds.makeOutset(kEntryPadding, kEntryPadding);

    // Request space in DrawAtlas.
    AtlasLocator locator;
    DrawAtlas::ErrorCode errorCode = fDrawAtlas->addRect(recorder,
                                                         iAtlasBounds.width(),
                                                         iAtlasBounds.height(),
                                                         &locator);
    if (errorCode != DrawAtlas::ErrorCode::kSucceeded) {
        return nullptr;
    }
    SkIPoint topLeft = locator.topLeft();
    *outPos = skvx::half2(topLeft.x()+kEntryPadding, topLeft.y()+kEntryPadding);

    // Rasterize path to backing pixmap.
    // This pixmap will be the size of the Plot that contains the given rect, not the entire atlas,
    // and hence the position we render at will be relative to that Plot.
    // The value of outPos is relative to the entire texture, to be used for texture coords.
    SkAutoPixmapStorage dst;
    SkIPoint renderPos = fDrawAtlas->prepForRender(locator, &dst);

    RasterMaskHelper helper(&dst);
    if (!helper.init(fDrawAtlas->plotSize())) {
        return nullptr;
    }
    // Offset to plot location and draw
    iShapeBounds.offset(renderPos.x(), renderPos.y());
    helper.drawShape(shape, transform, strokeRec, iShapeBounds);

    // Add locator to ShapeCache.
    fShapeCache.set(maskKey, locator);
    // Add key to Plot's ShapeKeyList.
    uint32_t index = shape_key_list_index(locator.plotLocator(), fDrawAtlas.get());
    ShapeKeyEntry* keyEntry = new ShapeKeyEntry();
    keyEntry->fKey = maskKey;
    fKeyLists[index].addToTail(keyEntry);

    fDrawAtlas->setLastUseToken(locator,
                                recorder->priv().tokenTracker()->nextFlushToken());

    return fDrawAtlas->getProxies()[locator.pageIndex()].get();
}

bool SmallPathAtlas::DrawAtlasMgr::recordUploads(DrawContext* dc, Recorder* recorder) {
    return fDrawAtlas->recordUploads(dc, recorder);
}

void SmallPathAtlas::DrawAtlasMgr::evict(PlotLocator plotLocator) {
    // Remove all entries for this Plot from the ShapeCache
    uint32_t index = shape_key_list_index(plotLocator, fDrawAtlas.get());
    ShapeKeyList::Iter iter;
    iter.init(fKeyLists[index], ShapeKeyList::Iter::kHead_IterStart);
    ShapeKeyEntry* currEntry;
    while ((currEntry = iter.get())) {
        iter.next();
        fShapeCache.remove(currEntry->fKey);
        fKeyLists[index].remove(currEntry);
        delete currEntry;
    }
}

void SmallPathAtlas::DrawAtlasMgr::postFlush(Recorder* recorder) {
    fDrawAtlas->compact(recorder->priv().tokenTracker()->nextFlushToken());
}

}  // namespace skgpu::graphite
