// Light/dark toggle shared by the content pages. Applied before first paint via an
// inline snippet in each page's <head> (see setTheme's twin there) so there's no
// flash of the wrong theme; this file only handles the button and cross-tab sync.
//
// Default follows the OS. An explicit choice is remembered in localStorage and wins
// from then on — including over a later OS change, which is the point of choosing.
(function () {
  'use strict';
  var KEY = 'rotaryReaderTheme_v1';

  function stored() {
    try { return localStorage.getItem(KEY); } catch (e) { return null; }
  }

  function systemTheme() {
    return window.matchMedia && window.matchMedia('(prefers-color-scheme: light)').matches
      ? 'light' : 'dark';
  }

  function current() {
    return document.documentElement.getAttribute('data-theme') || systemTheme();
  }

  function apply(theme) {
    document.documentElement.setAttribute('data-theme', theme);
    document.querySelectorAll('[data-theme-toggle]').forEach(function (btn) {
      // The button shows where you'd be going, not where you are.
      btn.textContent = theme === 'light' ? '◑ Dark' : '◐ Light';
      btn.setAttribute('aria-label', 'Switch to ' + (theme === 'light' ? 'dark' : 'light') + ' theme');
    });
  }

  function init() {
    apply(current());
    document.querySelectorAll('[data-theme-toggle]').forEach(function (btn) {
      btn.addEventListener('click', function () {
        var next = current() === 'light' ? 'dark' : 'light';
        try { localStorage.setItem(KEY, next); } catch (e) { /* private mode: this tab only */ }
        apply(next);
      });
    });
    // Only track the OS while the reader hasn't expressed a preference of their own.
    if (window.matchMedia) {
      window.matchMedia('(prefers-color-scheme: light)').addEventListener('change', function (e) {
        if (!stored()) apply(e.matches ? 'light' : 'dark');
      });
    }
    // Another tab toggled it — stay in sync rather than drifting apart.
    window.addEventListener('storage', function (e) {
      if (e.key === KEY && e.newValue) apply(e.newValue);
    });
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
