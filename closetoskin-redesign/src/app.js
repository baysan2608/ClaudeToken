(() => {
  const $ = (s, c = document) => c.querySelector(s);
  const $$ = (s, c = document) => [...c.querySelectorAll(s)];
  const SHOP = 'https://www.closetoskin.com';
  const KEY = 'cts-cart';
  const root = document.documentElement;
  const money = n => 'NOK ' + Number(n).toLocaleString('en-US', { maximumFractionDigits: 2 });
  const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  // review previews are served from a sub-path with relative links and no live checkout; production leaves CTS_ROOT unset
  const ROOT = window.CTS_ROOT;
  const PREVIEW = !!window.CTS_PREVIEW;
  const url = u => (ROOT == null || /^https?:/.test(u)) ? u : ROOT + u.replace(/^\//, '') + (u.endsWith('/') ? 'index.html' : '');

  /* ---------- cart (localStorage, checkout via Shopify cart permalink) ---------- */
  let cart = [];
  let index;
  try { cart = JSON.parse(localStorage.getItem(KEY) || '[]'); } catch (e) {}
  const save = () => { try { localStorage.setItem(KEY, JSON.stringify(cart)); } catch (e) {} render(); };
  let opener;
  const panelFor = cls => ({ 'cart-open': '.drawer', 'search-open': '.search', 'nav-open': '.nav' }[cls]);
  const open = cls => {
    opener = document.activeElement;
    root.classList.add(cls);
    if (cls === 'cart-open') recs();
    $$('[data-open-nav]').forEach(b => b.setAttribute('aria-expanded', cls === 'nav-open'));
  };
  const closeAll = () => {
    const wasOpen = ['cart-open', 'search-open', 'nav-open'].some(c => root.classList.contains(c));
    root.classList.remove('cart-open', 'search-open', 'nav-open');
    $$('[data-open-nav]').forEach(b => b.setAttribute('aria-expanded', 'false'));
    if (wasOpen && opener && opener.focus) opener.focus();
  };
  // keep keyboard focus inside whichever panel is open
  document.addEventListener('keydown', e => {
    if (e.key !== 'Tab') return;
    const cls = ['cart-open', 'search-open', 'nav-open'].find(c => root.classList.contains(c));
    if (!cls) return;
    const f = $$('a[href],button:not([disabled]),input', $(panelFor(cls))).filter(x => x.offsetParent !== null);
    if (!f.length) return;
    if (e.shiftKey && document.activeElement === f[0]) { e.preventDefault(); f[f.length - 1].focus(); }
    else if (!e.shiftKey && document.activeElement === f[f.length - 1]) { e.preventDefault(); f[0].focus(); }
  });

  function render() {
    const count = cart.reduce((a, x) => a + x.q, 0);
    $$('[data-cart-count]').forEach(b => { b.textContent = count || ''; });
    const lines = $('#cart-lines');
    if (!lines) return;
    if (!cart.length) {
      lines.innerHTML = `<p class="empty">Your bag is empty.<br><a class="link" href="${url('/collections/underwear/')}">Shop underwear</a></p>`;
      if (root.classList.contains('cart-open')) recs();
    } else {
      lines.innerHTML = cart.map((x, i) => `<div class="line"><img src="${esc(url(x.i))}" alt="" width="76" height="95" loading="lazy">
        <div><a href="${esc(url(x.u))}">${esc(x.t)}</a><div class="muted">${esc(x.v || '')}</div>
        <div class="qty"><button type="button" data-q="${i}" data-d="-1" aria-label="Decrease quantity">−</button><span>${x.q}</span><button type="button" data-q="${i}" data-d="1" aria-label="Increase quantity">+</button></div></div>
        <div style="text-align:right">${money(x.p * x.q)}<br><button type="button" class="rm" data-rm="${i}">Remove</button></div></div>`).join('');
      if (root.classList.contains('cart-open')) recs();
    }
    const total = cart.reduce((a, x) => a + x.p * x.q, 0);
    $('#cart-total').textContent = money(total);
    const co = $('#checkout');
    co.href = cart.length ? SHOP + '/cart/' + cart.map(x => x.id + ':' + x.q).join(',') : '#';
    co.toggleAttribute('aria-disabled', !cart.length);
  }

  // bestsellers that aren't in the bag yet, shown under the bag lines
  async function recs() {
    const lines = $('#cart-lines');
    if (!index) { try { index = await (await fetch(url('/search-index.json'))).json(); } catch (e) { return; } }
    const inBag = new Set(cart.map(x => x.u));
    const picks = index.filter(p => p.b && !inBag.has(p.u)).slice(0, 3);
    if (!picks.length || lines.querySelector('.recs')) return;
    lines.insertAdjacentHTML('beforeend', `<div class="recs"><h3>${cart.length ? 'Goes well with' : 'Bestsellers'}</h3><ul>${picks.map(p => `<li><a href="${url(p.u)}"><img src="${url(p.i)}" alt="" width="56" height="70" loading="lazy"><span>${esc(p.n)}<br><small class="muted">${esc(p.m)}</small></span><span>${money(p.p)}</span></a></li>`).join('')}</ul></div>`);
  }

  function add(item) {
    const found = cart.find(x => x.id === item.id);
    found ? (found.q += item.q) : cart.push(item);
    save();
    open('cart-open');
    $('#cart-close')?.focus();
  }

  document.addEventListener('click', e => {
    const t = e.target.closest('button,a');
    if (!t) return;
    if (t.matches('[data-open-cart]')) { e.preventDefault(); open('cart-open'); }
    else if (t.matches('[data-open-search]')) { e.preventDefault(); openSearch(); }
    else if (t.matches('[data-open-nav]')) { open('nav-open'); $('.nav-close')?.focus(); }
    else if (t.matches('.sub-toggle')) { const o = t.parentElement.classList.toggle('open'); t.setAttribute('aria-expanded', o); }
    else if (t.matches('[data-close]')) { closeAll(); }
    else if (t.dataset.q) { const x = cart[+t.dataset.q]; x.q += +t.dataset.d; if (x.q < 1) cart.splice(+t.dataset.q, 1); save(); }
    else if (t.dataset.rm) { cart.splice(+t.dataset.rm, 1); save(); }
    else if (t.id === 'checkout' && (PREVIEW || !cart.length)) {
      e.preventDefault();
      if (PREVIEW && cart.length) t.textContent = 'Checkout is turned off in this preview';
    }
  });
  if (PREVIEW) document.addEventListener('submit', e => {
    if (e.target.id === 'buy') return;
    e.preventDefault();
    const b = e.target.querySelector('[type=submit]');
    if (b) b.textContent = 'Turned off in this preview';
  });
  $('.overlay')?.addEventListener('click', closeAll);
  document.addEventListener('keydown', e => { if (e.key === 'Escape') closeAll(); });

  /* ---------- search (lazy-loaded index) ---------- */
  async function openSearch() {
    open('search-open');
    const input = $('#q');
    input.focus();
    if (!index) { try { index = await (await fetch(url('/search-index.json'))).json(); } catch (e) { index = []; } }
    input.dispatchEvent(new Event('input'));
  }
  $('#q')?.addEventListener('input', e => {
    const q = e.target.value.trim().toLowerCase();
    const out = $('#results');
    if (!index) return;
    const words = q.split(/\s+/).filter(Boolean);
    // bestsellers first, then products whose name (not just keywords) matches
    const score = p => (p.b ? 2 : 0) + (words.some(w => p.n.toLowerCase().includes(w)) ? 1 : 0);
    const hits = (words.length ? index.filter(p => words.every(w => p.k.includes(w))) : index.filter(p => p.b))
      .map((p, i) => [p, i]).sort((a, b) => score(b[0]) - score(a[0]) || a[1] - b[1]).map(x => x[0]).slice(0, 8);
    out.innerHTML = hits.length
      ? hits.map(p => `<li><a href="${url(p.u)}"><img src="${url(p.i)}" alt="" width="48" height="60" loading="lazy"><span>${esc(p.n)}<br><small>${esc(p.m)}</small></span><span>${money(p.p)}</span></a></li>`).join('')
      : `<li class="muted" style="padding:12px 8px">No matches for “${esc(q)}”.</li>`;
  });

  /* ---------- product page ---------- */
  const pdata = $('#pdata');
  if (pdata) {
    const P = JSON.parse(pdata.textContent);
    const form = $('#buy');
    const btns = $$('[data-atc]');
    const priceEls = $$('[data-price]');
    const stock = $('#stock');
    const pick = () => P.options.map((o, i) => (form.querySelector(`input[name="o${i}"]:checked`) || {}).value);
    const find = sel => P.variants.find(v => v.o.every((x, i) => x === sel[i]));
    let current;
    // the sticky bar asks the shopper to choose before it can add a default size
    let picked = P.variants.length < 2;
    const choose = P.options.some(o => /size/i.test(o)) ? 'Choose size' : 'Choose colour';
    function update(e) {
      if (e) picked = true;
      const sel = pick();
      current = find(sel);
      // mark unavailable combinations for the last option
      P.options.forEach((o, i) => {
        $$(`input[name="o${i}"]`, form).forEach(inp => {
          const test = sel.slice(); test[i] = inp.value;
          const v = find(test);
          inp.nextElementSibling.classList.toggle('na', !v || !v.a);
        });
        const lab = $(`#ov${i}`); if (lab) lab.textContent = sel[i] ? (P.labels[sel[i]] || sel[i]) : '';
      });
      const ok = current && current.a;
      btns.forEach(b => {
        const sticky = !!b.closest('.sticky-atc');
        b.disabled = !ok && !(sticky && !picked);
        b.textContent = sticky && !picked ? choose : ok ? (sticky ? 'Add to bag' : 'Add to bag — ' + money(current.p)) : (current ? 'Sold out' : 'Select options');
      });
      priceEls.forEach(p => { p.textContent = money(current ? current.p : P.price); });
      if (stock) { stock.textContent = ok ? 'In stock, ready to ship' : 'Currently sold out'; stock.classList.toggle('out', !ok); }
      if (e && current && P.variants.length > 1) try { history.replaceState(null, '', '?variant=' + current.id); } catch (err) {}
    }
    const pre = new URLSearchParams(location.search).get('variant');
    const preV = pre && P.variants.find(v => String(v.id) === pre);
    if (preV) preV.o.forEach((val, i) => { const inp = $$(`input[name="o${i}"]`, form).find(x => x.value === val); if (inp) inp.checked = true; });
    form.addEventListener('change', update);
    update();
    btns.forEach(b => b.addEventListener('click', e => {
      e.preventDefault();
      if (!picked) { form.scrollIntoView({ behavior: 'smooth', block: 'center' }); return; }
      if (!current || !current.a) { form.scrollIntoView({ behavior: 'smooth', block: 'center' }); return; }
      add({ id: current.id, q: 1, t: P.name, v: P.variants.length > 1 ? current.o.map(x => P.labels[x] || x).join(' / ') : '', p: current.p, i: P.img, u: P.url });
    }));

    // gallery: thumbnails + dots follow the scroll-snap slider
    const slides = $('.slides');
    const thumbs = $$('.thumbs button');
    const dots = $$('.dots i');
    thumbs.forEach((t, i) => t.addEventListener('click', () => slides.scrollTo({ left: slides.clientWidth * i, behavior: 'smooth' })));
    slides?.addEventListener('scroll', () => {
      const i = Math.round(slides.scrollLeft / slides.clientWidth);
      thumbs.forEach((t, j) => t.setAttribute('aria-current', i === j));
      dots.forEach((d, j) => d.classList.toggle('on', i === j));
    }, { passive: true });

    // sticky add-to-bag on mobile whenever the main button is off screen
    const bar = $('.sticky-atc');
    const main = $('#atc-main');
    if (bar && main && 'IntersectionObserver' in window) {
      new IntersectionObserver(([en]) => bar.classList.toggle('show', !en.isIntersecting)).observe(main);
    }
  }

  /* ---------- product cards: load the second image only when a mouse hovers ---------- */
  if (matchMedia('(hover: hover)').matches) {
    document.addEventListener('pointerover', e => {
      const m = e.target.closest && e.target.closest('.media[data-hover]');
      if (!m || m.dataset.done) return;
      m.dataset.done = '1';
      const img = new Image();
      img.src = m.dataset.hover; img.alt = ''; img.className = 'hover'; img.decoding = 'async';
      m.appendChild(img);
    });
  }

  /* ---------- collection filters + sort ---------- */
  const grid = $('#grid');
  if (grid) {
    const cards = $$('.card', grid);
    const state = { f: null, c: null };
    const count = $('#count');
    const apply = () => {
      let n = 0;
      cards.forEach(c => {
        const show = (!state.f || c.dataset.f.split(' ').includes(state.f)) && (!state.c || c.dataset.c.split(' ').includes(state.c));
        c.hidden = !show; if (show) n++;
      });
      if (count) count.textContent = n + (n === 1 ? ' product' : ' products');
    };
    $$('[data-filter]').forEach(b => b.addEventListener('click', () => {
      const [k, v] = b.dataset.filter.split(':');
      state[k] = state[k] === v ? null : v;
      $$(`[data-filter^="${k}:"]`).forEach(x => x.setAttribute('aria-pressed', x.dataset.filter === k + ':' + state[k]));
      apply();
    }));
    $('#sort')?.addEventListener('change', e => {
      const m = e.target.value;
      const sorted = cards.slice().sort((a, b) => m === 'asc' ? a.dataset.p - b.dataset.p : m === 'desc' ? b.dataset.p - a.dataset.p : m === 'new' ? b.dataset.d.localeCompare(a.dataset.d) : a.dataset.i - b.dataset.i);
      sorted.forEach(c => grid.appendChild(c));
    });
  }

  render();
})();
