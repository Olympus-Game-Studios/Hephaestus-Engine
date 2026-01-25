(function () {
  const listeners = new Map();

  function on(eventName, handler) {
    if (!eventName || !handler) return;
    if (!listeners.has(eventName)) listeners.set(eventName, new Set());
    listeners.get(eventName).add(handler);
  }

  function off(eventName, handler) {
    if (!eventName || !handler) return;
    const set = listeners.get(eventName);
    if (!set) return;
    set.delete(handler);
  }

  function emit(eventName, payload) {
    const set = listeners.get(eventName);
    if (!set) return;
    set.forEach(fn => {
      try { fn(payload); } catch (e) { console.warn('EditorAPI event handler error:', eventName, e); }
    });
  }

  function bridgeCall(name, payload) {
    if (window.hep && window.hep.callCpp) {
      return window.hep.callCpp(name, payload || {});
    }
    return { ok: false, error: 'bridge_unavailable', name: name };
  }

  const nativeApi = window.EditorAPI || {};

  const api = {
    on,
    off,
    emit,
    requestSceneTree() {
      if (typeof nativeApi.requestSceneTree === 'function') return nativeApi.requestSceneTree();
      return bridgeCall('scene.list', {});
    },
    selectEntity(entityId) {
      if (typeof nativeApi.selectEntity === 'function') return nativeApi.selectEntity(entityId);
      return bridgeCall('scene.select', { id: Number(entityId || 0), toggle: 0, add: 0 });
    },
    requestEntityDetails(entityId) {
      if (typeof nativeApi.requestEntityDetails === 'function') return nativeApi.requestEntityDetails(entityId);
      return bridgeCall('entity.get', { id: Number(entityId || 0) });
    },
    setEntityProperty(entityId, path, value) {
      if (typeof nativeApi.setEntityProperty === 'function') return nativeApi.setEntityProperty(entityId, path, value);
      return bridgeCall('entity.set_property', { id: Number(entityId || 0), path: String(path || ''), value: value });
    },
    requestAssets(filter) {
      if (typeof nativeApi.requestAssets === 'function') return nativeApi.requestAssets(filter || {});
      return bridgeCall('assets.list', filter || {});
    },
    createEntity(type, parentId, name) {
      if (typeof nativeApi.createEntity === 'function') return nativeApi.createEntity(type, parentId, name);
      return bridgeCall('entity.create', { type: String(type || ''), parentId: Number(parentId || 0), name: name ? String(name) : '' });
    },
    openBlueprint(assetId) {
      if (typeof nativeApi.openBlueprint === 'function') {
        const res = nativeApi.openBlueprint(assetId);
        emit('openBlueprint', { assetId: assetId });
        return res;
      }
      const res = bridgeCall('blueprint.open', { path: assetId, assetId: assetId });
      emit('openBlueprint', { assetId: assetId });
      return res;
    },
    log(level, message) {
      if (typeof nativeApi.log === 'function') return nativeApi.log(level, message);
      return bridgeCall('log', { level: String(level || 'info'), message: String(message || '') });
    },
    _bridgeCall: bridgeCall
  };

  window.EditorAPI = api;

  const existingEvents = window.EditorEvents || {};
  window.EditorEvents = {
    sceneTreeUpdated(treeJson) {
      if (typeof existingEvents.sceneTreeUpdated === 'function') existingEvents.sceneTreeUpdated(treeJson);
      emit('sceneTreeUpdated', treeJson);
    },
    selectionChanged(entityId) {
      if (typeof existingEvents.selectionChanged === 'function') existingEvents.selectionChanged(entityId);
      emit('selectionChanged', entityId);
    },
    entityDetailsUpdated(entityId, detailsJson) {
      if (typeof existingEvents.entityDetailsUpdated === 'function') existingEvents.entityDetailsUpdated(entityId, detailsJson);
      emit('entityDetailsUpdated', detailsJson || { ok: true, entity: null, entityId: entityId });
    },
    assetsUpdated(assetsJson) {
      if (typeof existingEvents.assetsUpdated === 'function') existingEvents.assetsUpdated(assetsJson);
      emit('assetsUpdated', assetsJson);
    },
    entityCreated(entityJson) {
      if (typeof existingEvents.entityCreated === 'function') existingEvents.entityCreated(entityJson);
      emit('entityCreated', entityJson);
    },
    logAppended(entryJson) {
      if (typeof existingEvents.logAppended === 'function') existingEvents.logAppended(entryJson);
      emit('logAppended', entryJson);
    }
  };

  window.hepReceive = function (msg) {
    if (!msg || msg.v !== 1) return;
    const payload = msg.payload;
    switch (msg.name) {
      case 'sceneTreeUpdated':
      case 'scene.list':
      case 'scene.tree':
        window.EditorEvents.sceneTreeUpdated(payload);
        break;
      case 'selectionChanged':
      case 'selection.changed':
        window.EditorEvents.selectionChanged(payload && payload.entityId ? payload.entityId : payload);
        break;
      case 'entityDetailsUpdated':
      case 'entity.details':
        window.EditorEvents.entityDetailsUpdated(payload && payload.entityId ? payload.entityId : 0, payload);
        break;
      case 'assetsUpdated':
      case 'assets.updated':
        window.EditorEvents.assetsUpdated(payload);
        break;
      case 'entityCreated':
        window.EditorEvents.entityCreated(payload);
        break;
      case 'logAppended':
        window.EditorEvents.logAppended(payload);
        break;
      case 'blueprint.open':
        window.EditorAPI.emit('openBlueprint', payload);
        break;
      default:
        break;
    }
  };
})();
