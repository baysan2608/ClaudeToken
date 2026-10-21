(() => {
  const $ = (s, c = document) => c.querySelector(s);
  const $$ = (s, c = document) => [...c.querySelectorAll(s)];
  const SHOP = 'https://www.closetoskin.com';
  const KEY = 'cts-cart';
  const root = document.documentElement;
  const money = n => 'NOK ' + Number(n).toLocaleString('en-US', { maximumFractionDigits: 2 });
  const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

  /* ---------- cart (localStorage, checkout via Shopify cart permalink) ---------- */
  let cart = [];
  try { cart = JSON.parse(localStorage.getItem(KEY) || '[]'); } catch (e) {}
  const save = () => { try { localStorage.setItem(KEY, JSON.stringify(cart)); } catch (e) {} render(); };
  const open = cls => { root.classList.add(cls); };
  const closeAll = () => root.classList.remove('cart-open', 'search-open', 'nav-open');

  function render() {
    const count = cart.reduce((a, x) => a + x.q, 0);
    $$('[data-cart-count]').forEach(b => { b.textContent = count || ''; });
    const lines = $('#cart-lines');
    if (!lines) return;
    if (!cart.length) {
      lines.innerHTML = '<p class="empty">Your bag is empty.<br><a class="link" href="/collections/underwear/">Shop underwear</a></p>';
    } else {
      lines.innerHTML = cart.map((x, i) => `<div class="line"><img src="${esc(x.i)}" alt="" width="76" height="95" loading="lazy">
        <div><a href="${esc(x.u)}">${esc(x.t)}</a><div class="muted">${esc(x.v || '')}</div>
        <div class="qty"><button type="button" data-q="${i}" data-d="-1" aria-label="Decrease quantity">−</button><span>${x.q}</span><button type="button" data-q="${i}" data-d="1" aria-label="Increase quantity">+</button></div></div>
        <div style="text-align:right">${money(x.p * x.q)}<br><button type="button" class="rm" data-rm="${i}">Remove</button></div></div>`).join('');
    }
    const total = cart.reduce((a, x) => a + x.p * x.q, 0);
    $('#cart-total').textContent = money(total);
    const co = $('#checkout');
    co.href = cart.length ? SHOP + '/cart/' + cart.map(x => x.id + ':' + x.q).join(',') : '#';
    co.toggleAttribute('aria-disabled', !cart.length);
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
    else if (t.matches('[data-open-nav]')) { open('nav-open'); }
    else if (t.matches('[data-close]')) { closeAll(); }
    else if (t.dataset.q) { const x = cart[+t.dataset.q]; x.q += +t.dataset.d; if (x.q < 1) cart.splice(+t.dataset.q, 1); save(); }
    else if (t.dataset.rm) { cart.splice(+t.dataset.rm, 1); save(); }
    else if (t.id === 'checkout' && !cart.length) { e.preventDefault(); }
  });
  $('.overlay')?.addEventListener('click', closeAll);
  document.addEventListener('keydown', e => { if (e.key === 'Escape') closeAll(); });

  /* ---------- search (lazy-loaded index) ---------- */
  let index;
  async function openSearch() {
    open('search-open');
    const input = $('#q');
    input.focus();
    if (!index) { try { index = await (await fetch('/search-index.json')).json(); } catch (e) { index = []; } }
    input.dispatchEvent(new Event('input'));
  }
  $('#q')?.addEventListener('input', e => {
    const q = e.target.value.trim().toLowerCase();
    const out = $('#results');
    if (!index) return;
    const words = q.split(/\s+/).filter(Boolean);
    const hits = (words.length ? index.filter(p => words.every(w => p.k.includes(w))) : index.filter(p => p.b)).slice(0, 8);
    out.innerHTML = hits.length
      ? hits.map(p => `<li><a href="${p.u}"><img src="${p.i}" alt="" width="48" height="60" loading="lazy"><span>${esc(p.n)}<br><small>${esc(p.m)}</small></span><span>${money(p.p)}</span></a></li>`).join('')
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
      if (stock) { stock.textContent = ok ? 'In stock — ships within 1 business day' : 'Currently sold out'; stock.classList.toggle('out', !ok); }
      if (e && current && P.variants.length > 1) history.replaceState(null, '', '?variant=' + current.id);
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
      add({ id: current.id, q: 1, t: P.name, v: P.variants.length > 1 ? current.o.map(x => P.labels[x] || x).join(' / ') : '', p: current.p, i: P.img, u: location.pathname });
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
