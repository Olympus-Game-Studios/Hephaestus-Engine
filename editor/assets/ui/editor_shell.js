(function () {
  const state = {
    sceneTree: null,
    selectionId: 0,
    assets: null,
    activeBlueprintId: null
  };

  const hosts = {};
  let focusedPanelId = null;
  let projectCheckTimer = 0;
  let projectManagerVisible = false;

  // Project manager state (direct, no iframe)
  const pmState = {
    projects: [],
    templates: [],
    root: '',
    view: 'grid',
    sort: 'lastModified',
    search: '',
    selectedPath: ''
  };

  function showProjectManager() {
    const overlay = document.getElementById('panel-project-manager');
    if (overlay) overlay.classList.add('is-open');
    projectManagerVisible = true;
    if (projectCheckTimer) {
      clearInterval(projectCheckTimer);
      projectCheckTimer = 0;
    }
  }

  function hideProjectManager() {
    const overlay = document.getElementById('panel-project-manager');
    if (overlay) overlay.classList.remove('is-open');
    projectManagerVisible = false;
  }

  function syncProjectState() {
    if (!window.hep || !window.hep.callCpp) return;
    if (projectManagerVisible) return;
    const raw = window.hep.callCpp('project.is_selected', {});
    const resp = typeof raw === 'string' ? (function() { try { return JSON.parse(raw); } catch(e) { return null; } })() : raw;
    if (resp && resp.ok && resp.selected) {
      hideProjectManager();
      if (projectCheckTimer) {
        clearInterval(projectCheckTimer);
        projectCheckTimer = 0;
      }
    } else {
      showProjectManager();
    }
  }

  function broadcastState(diff) {
    Object.values(hosts).forEach(host => {
      if (host && host.notifyState) host.notifyState(diff);
    });
  }

  // ---------- Direct Project Manager (no iframe) ----------
  function pmFormatDate(ts) {
    if (!ts) return '';
    const d = new Date(ts * 1000);
    if (Number.isNaN(d.getTime())) return '';
    return d.toLocaleDateString() + ' ' + d.toLocaleTimeString();
  }

  function pmCompareProjects(a, b) {
    switch (pmState.sort) {
      case 'name': return a.name.localeCompare(b.name);
      case 'created': return (b.created || 0) - (a.created || 0);
      case 'favorites':
        if (a.favorite !== b.favorite) return a.favorite ? -1 : 1;
        return (b.lastModified || 0) - (a.lastModified || 0);
      default: return (b.lastModified || 0) - (a.lastModified || 0);
    }
  }

  function pmFilterProjects(list) {
    const q = pmState.search.trim().toLowerCase();
    if (!q) return list;
    return list.filter((p) => {
      const hay = [p.name, p.description, ...(p.tags || [])].join(' ').toLowerCase();
      return hay.includes(q);
    });
  }

  function pmSetStatus(text) {
    const el = document.getElementById('pm-status-text');
    if (el) el.textContent = text || 'Ready.';
  }

  function pmRenderProjects() {
    const container = document.getElementById('pm-projects-container');
    const emptyState = document.getElementById('pm-empty-state');
    if (!container) return;

    const filtered = pmFilterProjects(pmState.projects).sort(pmCompareProjects);
    container.className = pmState.view === 'grid' ? 'pm-projects-grid' : 'pm-projects-list';
    container.innerHTML = '';

    if (!filtered.length) {
      if (emptyState) emptyState.style.display = 'block';
      return;
    }
    if (emptyState) emptyState.style.display = 'none';

    filtered.forEach((project) => {
      const card = document.createElement('div');
      card.className = 'pm-project-card';
      // Store path directly on the element for reliable access
      card.dataset.projectPath = project.path || '';
      console.warn('[PM] Creating card for project: ' + project.name + ' path: ' + project.path);
      if (pmState.selectedPath && pmState.selectedPath === project.path) {
        card.classList.add('is-selected');
      }
      card.setAttribute('tabindex', '0');

      card.addEventListener('dblclick', (e) => {
        const cardEl = e.currentTarget;
        const pathFromData = cardEl.dataset.projectPath;
        console.warn('[PM] Card dblclick, path from dataset: ' + pathFromData);
        pmOpenProject(pathFromData);
      });
      card.addEventListener('click', (e) => {
        e.stopPropagation();
        const cardEl = e.currentTarget;
        const pathFromData = cardEl.dataset.projectPath;
        pmState.selectedPath = pathFromData;
        pmSetStatus('Selected: ' + project.name);
        container.querySelectorAll('.pm-project-card.is-selected').forEach((el) => el.classList.remove('is-selected'));
        cardEl.classList.add('is-selected');
      });

      const header = document.createElement('div');
      header.className = 'pm-project-header';

      const title = document.createElement('div');
      title.className = 'pm-project-title';
      title.textContent = project.name || 'Untitled Project';

      const fav = document.createElement('button');
      fav.className = 'pm-project-fav' + (project.favorite ? ' is-active' : '');
      fav.textContent = project.favorite ? '★' : '☆';
      fav.addEventListener('click', (e) => {
        e.stopPropagation();
        pmToggleFavorite(project);
      });

      header.appendChild(title);
      header.appendChild(fav);

      const desc = document.createElement('div');
      desc.className = 'pm-project-desc';
      desc.textContent = project.description || 'No description';

      const tags = document.createElement('div');
      tags.className = 'pm-project-meta';
      (project.tags || []).forEach((tag) => {
        const chip = document.createElement('span');
        chip.className = 'pm-project-tag';
        chip.textContent = tag;
        tags.appendChild(chip);
      });

      const footer = document.createElement('div');
      footer.className = 'pm-project-footer';
      const left = document.createElement('span');
      left.textContent = project.engineVersion ? `Engine ${project.engineVersion}` : '';
      const right = document.createElement('span');
      right.textContent = pmFormatDate(project.lastModified);
      footer.appendChild(left);
      footer.appendChild(right);

      card.appendChild(header);
      card.appendChild(desc);
      if ((project.tags || []).length) card.appendChild(tags);
      card.appendChild(footer);

      card.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') pmOpenProject(project.path);
      });
      container.appendChild(card);
    });
  }

  function pmPopulateTemplates() {
    const select = document.getElementById('pm-project-template');
    if (!select) return;
    select.innerHTML = '';
    pmState.templates.forEach((tpl) => {
      const opt = document.createElement('option');
      opt.value = String(tpl.index);
      opt.textContent = tpl.name;
      select.appendChild(opt);
    });
  }

  function pmIsThenable(obj) {
    return !!obj && (typeof obj === 'object' || typeof obj === 'function') && typeof obj.then === 'function';
  }

  function pmParseResponse(resp) {
    // If it's a string, try to parse it as JSON
    if (typeof resp === 'string') {
      try {
        return JSON.parse(resp);
      } catch (e) {
        return { ok: false, error: 'bad_json', raw: resp };
      }
    }
    return resp;
  }

  function pmNormalizeListResponse(resp) {
    if (!resp) return { ok: false, error: 'empty_response' };
    if (resp.ok === true) return resp;
    // Accept legacy shape without ok field
    if (resp.projects || resp.templates || resp.root) {
      return Object.assign({ ok: true }, resp);
    }
    return resp;
  }

  async function pmRefreshProjects() {
    pmSetStatus('Loading projects...');
    
    // Direct hepSend call to bypass any wrapper issues
    console.warn('[PM] pmRefreshProjects called, hepSend available: ' + (!!window.hepSend));
    
    if (!window.hepSend) {
      pmSetStatus('No hepSend bridge available.');
      console.warn('[PM] hepSend not available, cannot load projects');
      return;
    }
    
    try {
      const msg = JSON.stringify({ v: 1, name: 'project.list', payload: {} });
      console.warn('[PM] Calling hepSend with: ' + msg);
      const respStr = window.hepSend(msg);
      console.warn('[PM] hepSend returned type: ' + (typeof respStr) + ' length: ' + (respStr ? respStr.length : 0));
      console.warn('[PM] hepSend returned first 500 chars: ' + String(respStr).substring(0, 500));
      
      const resp = JSON.parse(respStr);
      console.warn('[PM] Parsed response ok: ' + (resp ? resp.ok : 'null'));
      console.warn('[PM] Parsed response has projects: ' + (resp && resp.projects ? resp.projects.length : 'no'));
      
      if (!resp || !resp.ok) {
        pmSetStatus('Failed to load projects.' + (resp && resp.error ? ' (' + resp.error + ')' : ''));
        return;
      }
      pmState.projects = resp.projects || [];
      pmState.templates = resp.templates || [];
      pmState.root = resp.root || '';
      console.warn('[PM] Loaded ' + pmState.projects.length + ' projects');
      pmState.projects.forEach((p, i) => {
        console.warn('[PM] Project ' + i + ': name=' + p.name + ' path=' + p.path + ' keys=' + Object.keys(p).join(','));
      });
      const rootLabel = document.getElementById('pm-root-label');
      if (rootLabel) rootLabel.textContent = pmState.root ? `Projects: ${pmState.root}` : 'Projects';
      pmPopulateTemplates();
      pmRenderProjects();
      pmSetStatus(`Loaded ${pmState.projects.length} project(s).`);
    } catch (e) {
      console.error('[PM] Exception in pmRefreshProjects:', e);
      pmSetStatus('Failed to load projects: ' + e);
    }
  }

  async function pmOpenProject(path) {
    console.warn('[PM] pmOpenProject called with path: ' + path);
    if (!path) {
      console.warn('[PM] pmOpenProject: no path provided');
      return;
    }
    if (!window.hep || !window.hep.callCpp) {
      console.warn('[PM] pmOpenProject: no hep.callCpp available');
      pmSetStatus('Failed to open project: no bridge');
      return;
    }
    try {
      console.warn('[PM] Calling project.open with path: ' + path);
      const callResult = window.hep.callCpp('project.open', { path });
      console.warn('[PM] project.open callResult type: ' + (typeof callResult));
      const rawResp = pmIsThenable(callResult) ? await callResult : callResult;
      const resp = pmParseResponse(rawResp);
      console.warn('[PM] project.open parsed resp: ' + JSON.stringify(resp));
      if (resp && resp.ok) {
        pmSetStatus('Opening project...');

        const waitForSelection = async () => {
          const maxAttempts = 50;
          for (let i = 0; i < maxAttempts; i++) {
            const selRaw = window.hep.callCpp('project.is_selected', {});
            const selResp = pmParseResponse(pmIsThenable(selRaw) ? await selRaw : selRaw);
            console.warn('[PM] is_selected attempt ' + i + ': ' + JSON.stringify(selResp));
            if (selResp && selResp.ok && selResp.selected) {
              if (!path || !selResp.root) return true;
              const expected = String(path).replace(/\\/g, '/').toLowerCase();
              const actual = String(selResp.root).replace(/\\/g, '/').toLowerCase();
              console.warn('[PM] Comparing paths - expected: ' + expected + ' actual: ' + actual);
              if (actual === expected) return true;
            }
            await new Promise(r => setTimeout(r, 100));
          }
          return false;
        };

        const opened = await waitForSelection();
        console.warn('[PM] waitForSelection result: ' + opened);
        if (!opened) {
          pmSetStatus('Failed to open project (timeout).');
          return;
        }

        hideProjectManager();
        if (window.EditorAPI && window.EditorAPI.requestSceneTree) {
          const res = window.EditorAPI.requestSceneTree();
          if (res) window.EditorEvents.sceneTreeUpdated(res);
        }
        if (window.EditorAPI && window.EditorAPI.requestAssets) {
          const res = window.EditorAPI.requestAssets({});
          if (res) window.EditorEvents.assetsUpdated(res);
        }
      } else {
        console.warn('[PM] project.open failed, resp: ' + JSON.stringify(resp));
        pmSetStatus('Failed to open project.' + (resp && resp.error ? ' (' + resp.error + ')' : ''));
      }
    } catch (e) {
      console.error('[PM] pmOpenProject exception: ' + e);
      pmSetStatus('Failed to open project: ' + e);
    }
  }

  async function pmToggleFavorite(project) {
    if (!window.hep || !window.hep.callCpp) return;
    try {
      const callResult = window.hep.callCpp('project.favorite', { path: project.path, value: project.favorite ? 0 : 1 });
      const rawResp = pmIsThenable(callResult) ? await callResult : callResult;
      const resp = pmParseResponse(rawResp);
      if (resp && resp.ok) {
        project.favorite = !project.favorite;
        pmRenderProjects();
      }
    } catch (e) {
      console.warn('pmToggleFavorite failed:', e);
    }
  }

  function pmOpenModal() {
    const modal = document.getElementById('pm-new-project-modal');
    if (modal) modal.classList.add('is-open');
    const nameInput = document.getElementById('pm-project-name');
    if (nameInput) nameInput.focus();
  }

  function pmCloseModal() {
    const modal = document.getElementById('pm-new-project-modal');
    if (modal) modal.classList.remove('is-open');
  }

  async function pmCreateProject() {
    const nameInput = document.getElementById('pm-project-name');
    const descInput = document.getElementById('pm-project-desc');
    const templateSelect = document.getElementById('pm-project-template');
    const locationInput = document.getElementById('pm-project-location');
    const name = nameInput ? nameInput.value.trim() : '';
    const description = descInput ? descInput.value.trim() : '';
    const templateIndex = templateSelect ? parseInt(templateSelect.value, 10) : -1;
    const location = locationInput ? locationInput.value.trim() : '';

    if (!name) {
      pmSetStatus('Project name is required.');
      return;
    }
    if (!window.hep || !window.hep.callCpp) return;

    try {
      const callResult = window.hep.callCpp('project.create', {
        name,
        description,
        templateIndex: Number.isFinite(templateIndex) ? templateIndex : -1,
        location
      });
      const rawResp = pmIsThenable(callResult) ? await callResult : callResult;
      const resp = pmParseResponse(rawResp);
      if (resp && resp.ok) {
        pmCloseModal();
        if (nameInput) nameInput.value = '';
        if (descInput) descInput.value = '';
        if (locationInput) locationInput.value = '';
        await pmRefreshProjects();
        pmSetStatus('Project created.');
      } else {
        pmSetStatus((resp && resp.error) || 'Failed to create project.');
      }
    } catch (e) {
      pmSetStatus('Failed to create project: ' + e);
    }
  }

  async function pmBrowseLocation() {
    const locationInput = document.getElementById('pm-project-location');
    const base = (locationInput ? locationInput.value.trim() : '') || pmState.root || '';
    if (!window.hep || !window.hep.callCpp) return;
    try {
      const callResult = window.hep.callCpp('project.pick_folder', { path: base });
      const rawResp = pmIsThenable(callResult) ? await callResult : callResult;
      const resp = pmParseResponse(rawResp);
      if (resp && resp.ok && resp.path && locationInput) {
        locationInput.value = resp.path;
      }
    } catch (e) {
      console.warn('pmBrowseLocation failed:', e);
    }
  }

  function initProjectManagerDirect() {
    console.warn('[PM INIT] initProjectManagerDirect called');
    // Inject project manager HTML directly into the overlay container
    const overlay = document.getElementById('panel-project-manager');
    console.warn('[PM INIT] overlay element found:', !!overlay);
    if (!overlay) return;

    overlay.innerHTML = `
      <div class="pm-root">
        <header class="pm-header">
          <div class="pm-brand">
            <div class="pm-title">Hephaestus Engine</div>
            <div class="pm-subtitle" id="pm-root-label">Projects</div>
          </div>
          <div class="pm-header-actions">
            <button class="pm-btn primary" id="pm-new-btn">+ New Project</button>
            <button class="pm-btn" id="pm-refresh-btn">Refresh</button>
            <button class="pm-btn" id="pm-open-root-btn">Open Folder</button>
          </div>
        </header>
        <div class="pm-controls">
          <div class="pm-search">
            <input id="pm-search-input" placeholder="Search projects, tags..." />
          </div>
          <select id="pm-sort-select" class="pm-select">
            <option value="lastModified">Last Modified</option>
            <option value="name">Name</option>
            <option value="created">Created</option>
            <option value="favorites">Favorites</option>
          </select>
          <div class="pm-view-toggle" id="pm-view-toggle">
            <button data-view="grid" class="is-active">Grid</button>
            <button data-view="list">List</button>
          </div>
        </div>
        <div class="pm-content">
          <div class="pm-scroll">
            <div id="pm-projects-container" class="pm-projects-grid"></div>
            <div id="pm-empty-state" class="pm-empty" style="display:none;">
              No projects found. Create a new project to get started.
            </div>
          </div>
          <div class="pm-status" id="pm-status-text">Ready.</div>
        </div>
      </div>
      <div class="pm-modal" id="pm-new-project-modal">
        <div class="pm-modal-card">
          <h3>Create New Project</h3>
          <div class="pm-field"><label>Project Name</label><input id="pm-project-name" placeholder="My Awesome Game" /></div>
          <div class="pm-field"><label>Description</label><textarea id="pm-project-desc" placeholder="Short description..."></textarea></div>
          <div class="pm-field"><label>Template</label><select id="pm-project-template"></select></div>
          <div class="pm-field"><label>Location (optional)</label><div class="pm-row"><input id="pm-project-location" placeholder="Default projects folder" /><button class="pm-btn" id="pm-browse-btn">Browse</button></div></div>
          <div class="pm-modal-actions">
            <button class="pm-btn" id="pm-cancel-btn">Cancel</button>
            <button class="pm-btn primary" id="pm-create-btn">Create</button>
          </div>
        </div>
      </div>
    `;

    // Bind events
    const refreshBtn = document.getElementById('pm-refresh-btn');
    if (refreshBtn) refreshBtn.addEventListener('click', async () => {
      if (window.hep && window.hep.callCpp) {
        const callResult = window.hep.callCpp('project.refresh', {});
        if (pmIsThenable(callResult)) await callResult;
      }
      await pmRefreshProjects();
    });

    const openRootBtn = document.getElementById('pm-open-root-btn');
    if (openRootBtn) openRootBtn.addEventListener('click', async () => {
      if (window.hep && window.hep.callCpp) {
        const callResult = window.hep.callCpp('project.open_root', {});
        if (pmIsThenable(callResult)) await callResult;
      }
    });

    const newBtn = document.getElementById('pm-new-btn');
    if (newBtn) newBtn.addEventListener('click', pmOpenModal);

    const cancelBtn = document.getElementById('pm-cancel-btn');
    if (cancelBtn) cancelBtn.addEventListener('click', pmCloseModal);

    const createBtn = document.getElementById('pm-create-btn');
    if (createBtn) createBtn.addEventListener('click', pmCreateProject);

    const browseBtn = document.getElementById('pm-browse-btn');
    if (browseBtn) browseBtn.addEventListener('click', pmBrowseLocation);

    const searchInput = document.getElementById('pm-search-input');
    if (searchInput) searchInput.addEventListener('input', (e) => {
      pmState.search = e.target.value || '';
      pmRenderProjects();
    });

    const sortSelect = document.getElementById('pm-sort-select');
    if (sortSelect) sortSelect.addEventListener('change', (e) => {
      pmState.sort = e.target.value;
      pmRenderProjects();
    });

    const viewToggle = document.getElementById('pm-view-toggle');
    if (viewToggle) viewToggle.addEventListener('click', (e) => {
      const btn = e.target.closest('button');
      if (!btn) return;
      const view = btn.dataset.view;
      if (!view) return;
      pmState.view = view;
      viewToggle.querySelectorAll('button').forEach((b) => {
        b.classList.toggle('is-active', b.dataset.view === view);
      });
      pmRenderProjects();
    });

    const modal = document.getElementById('pm-new-project-modal');
    if (modal) modal.addEventListener('click', (e) => {
      if (e.target === modal) pmCloseModal();
    });

    // Initial load
    console.warn('[PM INIT] Checking for window.hep.onReady:', !!(window.hep && typeof window.hep.onReady === 'function'));
    if (window.hep && typeof window.hep.onReady === 'function') {
      console.warn('[PM INIT] Calling window.hep.onReady');
      window.hep.onReady(async () => {
        console.warn('[PM INIT] onReady callback fired, calling pmRefreshProjects');
        await pmRefreshProjects();
      });
    } else {
      console.warn('[PM INIT] No onReady, calling pmRefreshProjects directly');
      pmRefreshProjects();
    }
  }
  // ---------- End Direct Project Manager ----------

  function setFocusedPanel(panelId) {
    if (focusedPanelId === panelId) return;
    if (focusedPanelId && hosts[focusedPanelId]) {
      hosts[focusedPanelId].blur();
    }
    focusedPanelId = panelId;
    if (panelId && hosts[panelId]) {
      hosts[panelId].focus();
    }
  }

  function reportViewportSlot() {
    const slot = document.getElementById('viewport-slot');
    if (!slot) return;
    const rect = slot.getBoundingClientRect();
    const payload = {
      x: Math.round(rect.left),
      y: Math.round(rect.top),
      w: Math.round(rect.width),
      h: Math.round(rect.height)
    };
    if (window.EditorAPI && typeof window.EditorAPI.setViewportRect === 'function') {
      window.EditorAPI.setViewportRect(payload);
    } else if (window.hep && window.hep.callCpp) {
      window.hep.callCpp('viewport.set_rect', payload);
    }
  }

  function openBlueprint(assetId) {
    const overlay = document.getElementById('panel-blueprint');
    if (!overlay) return;
    overlay.classList.add('is-open');
    if (hosts.blueprint) hosts.blueprint.setVisible(true);
    state.activeBlueprintId = assetId || null;
    broadcastState({ activeBlueprintId: state.activeBlueprintId });
    setFocusedPanel('blueprint');
  }

  function closeBlueprint() {
    const overlay = document.getElementById('panel-blueprint');
    if (!overlay) return;
    overlay.classList.remove('is-open');
    if (hosts.blueprint) hosts.blueprint.setVisible(false);
    if (focusedPanelId === 'blueprint') setFocusedPanel('assetBrowser');
  }

  function initPanels() {
    hosts.outliner = new PanelHost({ panelId: 'outliner', url: './panels/outliner.html', container: document.getElementById('panel-outliner') });
    hosts.addActors = new PanelHost({ panelId: 'addActors', url: './panels/add_actors.html', container: document.getElementById('panel-add-actors') });
    hosts.assetBrowser = new PanelHost({ panelId: 'assetBrowser', url: './panels/asset_browser.html', container: document.getElementById('panel-asset-browser') });
    hosts.details = new PanelHost({ panelId: 'details', url: './panels/details.html', container: document.getElementById('panel-details') });
    hosts.log = new PanelHost({ panelId: 'log', url: './panels/log_console.html', container: document.getElementById('panel-log') });
    hosts.blueprint = new PanelHost({ panelId: 'blueprint', url: './panels/blueprint_editor.html', container: document.getElementById('panel-blueprint') });
    // Project manager loaded directly, not via iframe
    hosts.projectManager = null;

    Object.values(hosts).forEach(host => {
      if (!host) return;
      host.onFocusRequest = setFocusedPanel;
      host.load();
    });

    // Ensure blueprint panel starts closed
    const blueprintOverlay = document.getElementById('panel-blueprint');
    if (blueprintOverlay) {
      blueprintOverlay.classList.remove('is-open');
    }
    if (hosts.blueprint) hosts.blueprint.setVisible(false);
    
    // Initialize project manager directly
    initProjectManagerDirect();
  }

  function initEvents() {
    if (window.EditorAPI && window.EditorAPI.on) {
      window.EditorAPI.on('sceneTreeUpdated', (tree) => {
        state.sceneTree = tree;
        broadcastState({ sceneTree: tree });
      });
      window.EditorAPI.on('selectionChanged', (entityId) => {
        state.selectionId = Number(entityId || 0);
        broadcastState({ selectionId: state.selectionId });
      });
      window.EditorAPI.on('assetsUpdated', (assetsJson) => {
        state.assets = assetsJson;
        broadcastState({ assets: assetsJson });
      });
      window.EditorAPI.on('entityCreated', (entityJson) => {
        broadcastState({ entityCreated: entityJson });
        if (window.EditorAPI.requestSceneTree) {
          window.EditorAPI.requestSceneTree();
        }
      });
      window.EditorAPI.on('openBlueprint', (payload) => {
        if (payload && payload.assetId) openBlueprint(payload.assetId);
      });
    }
  }

  function initToolbar() {
    const btnOpenBlueprint = document.getElementById('btn-open-blueprint');
    if (btnOpenBlueprint) {
      btnOpenBlueprint.onclick = () => openBlueprint(state.activeBlueprintId || 'assets/blueprints/Player.hpblueprint');
    }
    const btnRefreshScene = document.getElementById('btn-refresh-scene');
    if (btnRefreshScene) {
      btnRefreshScene.onclick = () => {
        if (window.EditorAPI && window.EditorAPI.requestSceneTree) {
          const res = window.EditorAPI.requestSceneTree();
          if (res) {
            window.EditorEvents.sceneTreeUpdated(res);
          }
        }
      };
    }

    const overlay = document.getElementById('panel-blueprint');
    if (overlay) {
      overlay.addEventListener('dblclick', closeBlueprint);
    }
  }

  function initResize() {
    const slot = document.getElementById('viewport-slot');
    if (!slot) return;
    const ro = new ResizeObserver(() => {
      reportViewportSlot();
      Object.values(hosts).forEach(host => host.resize());
    });
    ro.observe(slot);
    window.addEventListener('resize', () => {
      reportViewportSlot();
      Object.values(hosts).forEach(host => host.resize());
    });
    reportViewportSlot();
  }

  function init() {
    console.warn('[SHELL INIT] init() called');
    initPanels();
    console.warn('[SHELL INIT] initPanels() completed');
    initEvents();
    initToolbar();
    initResize();
    syncProjectState();
    setFocusedPanel('viewport');
    console.warn('[SHELL INIT] init() completed');
  }

  // Listen for messages from iframes for C++ bridge relay
  window.addEventListener('message', (event) => {
    const data = event && event.data ? event.data : null;
    if (!data) return;

    // Handle project opened notification
    if (data.type === 'hep_project_opened') {
      hideProjectManager();
      if (projectCheckTimer) {
        clearInterval(projectCheckTimer);
        projectCheckTimer = 0;
      }
      if (window.EditorAPI && window.EditorAPI.requestSceneTree) {
        const res = window.EditorAPI.requestSceneTree();
        if (res) window.EditorEvents.sceneTreeUpdated(res);
      }
      if (window.EditorAPI && window.EditorAPI.requestAssets) {
        const res = window.EditorAPI.requestAssets({});
        if (res) window.EditorEvents.assetsUpdated(res);
      }
      return;
    }

    // Handle C++ bridge relay requests from iframes
    if (data.type === 'hep_callCpp_request' && data.callId !== undefined) {
      const { callId, name, payload } = data;
      let response = { ok: false, error: 'no_bridge' };
      if (window.hep && window.hep.callCpp) {
        try {
          response = window.hep.callCpp(name, payload);
        } catch (e) {
          response = { ok: false, error: String(e) };
        }
      }
      // Send response back to the iframe
      if (event.source) {
        event.source.postMessage({ type: 'hep_callCpp_response', callId, response }, '*');
      }
    }
  });

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
