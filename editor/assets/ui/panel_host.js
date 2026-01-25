(function () {
  class PanelHost {
    constructor(options) {
      this.panelId = options.panelId;
      this.url = options.url;
      this.container = options.container;
      this.iframe = null;
      this.controller = null;
      this.visible = true;
      this._focused = false;
    }

    load() {
      if (!this.container) return;
      const iframe = document.createElement('iframe');
      iframe.className = 'panel-frame';
      iframe.src = this.url;
      iframe.setAttribute('frameborder', '0');
      iframe.onload = () => {
        try {
          const win = iframe.contentWindow;
          this.controller = win && win.PanelController ? win.PanelController : null;
          if (this.controller && typeof this.controller.init === 'function') {
            this.controller.init(this.panelId, window.EditorAPI);
          }
          this.resize();
        } catch (e) {
          console.warn('PanelHost init failed:', this.panelId, e);
        }
      };
      this.container.innerHTML = '';
      this.container.appendChild(iframe);
      this.iframe = iframe;

      this.container.addEventListener('pointerdown', () => {
        if (this.onFocusRequest) this.onFocusRequest(this.panelId);
      });
    }

    resize() {
      if (!this.container || !this.controller || typeof this.controller.onResize !== 'function') return;
      const rect = this.container.getBoundingClientRect();
      this.controller.onResize(rect.width, rect.height);
    }

    setVisible(visible) {
      this.visible = !!visible;
      if (this.container) {
        this.container.style.display = this.visible ? '' : 'none';
      }
      if (!this.visible && this._focused) {
        this.blur();
      }
    }

    focus() {
      if (this._focused) return;
      this._focused = true;
      if (this.controller && typeof this.controller.onFocus === 'function') {
        this.controller.onFocus();
      }
    }

    blur() {
      if (!this._focused) return;
      this._focused = false;
      if (this.controller && typeof this.controller.onBlur === 'function') {
        this.controller.onBlur();
      }
    }

    notifyState(stateDiff) {
      if (this.controller && typeof this.controller.onStateUpdate === 'function') {
        this.controller.onStateUpdate(stateDiff);
      }
    }
  }

  window.PanelHost = PanelHost;
})();
