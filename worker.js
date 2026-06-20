/* Web Worker untuk pemrosesan file besar */

function normalize(line, opts) {
  let l = line;
  if (opts.trim) l = l.trim();
  if (opts.ignoreCase) l = l.toLowerCase();
  return l;
}

function splitLines(text, opts) {
  const raw = text.split(/\r\n|\r|\n/);
  const lines = [];
  for (let idx = 0; idx < raw.length; idx++) {
    const line = raw[idx];
    const norm = normalize(line, opts);
    if (opts.ignoreBlank && norm === '') continue;
    lines.push({ original: line, norm: norm, lineNum: idx + 1 });
  }
  return lines;
}

function diff(source, target) {
  const remaining = new Map();
  for (let i = 0; i < target.length; i++) {
    const norm = target[i].norm;
    remaining.set(norm, (remaining.get(norm) || 0) + 1);
  }
  const out = [];
  for (let i = 0; i < source.length; i++) {
    const l = source[i];
    const avail = remaining.get(l.norm) || 0;
    if (avail > 0) {
      remaining.set(l.norm, avail - 1);
    } else {
      out.push(l);
    }
  }
  return out;
}

function extractEmails(text) {
  const EMAIL_RE = /[a-zA-Z0-9._%+\-]+@[a-zA-Z0-9.\-]+\.[a-zA-Z]{2,}/g;
  const found = text.match(EMAIL_RE) || [];
  const result = [];
  for (let i = 0; i < found.length; i++) {
    const email = found[i].trim();
    const domain = email.slice(email.lastIndexOf('@') + 1).toLowerCase();
    result.push({ email, domain });
  }
  return result;
}

self.onmessage = (evt) => {
  const { type, text1, text2, textE, opts, id } = evt.data;

  try {
    if (type === 'compare') {
      const lines1 = splitLines(text1, opts);
      const lines2 = splitLines(text2, opts);
      const missing = diff(lines1, lines2);
      const added = diff(lines2, lines1);
      self.postMessage({
        type: 'compare',
        id,
        lines1Count: lines1.length,
        lines2Count: lines2.length,
        missing,
        added,
      });
    } else if (type === 'email') {
      const emails = extractEmails(textE);
      const counts = new Map();
      for (let i = 0; i < emails.length; i++) {
        const domain = emails[i].domain;
        counts.set(domain, (counts.get(domain) || 0) + 1);
      }
      const sorted = [...counts.entries()].sort((a, b) => b[1] - a[1]);
      self.postMessage({
        type: 'email',
        id,
        emails,
        domainCounts: Object.fromEntries(sorted),
        domains: sorted.map(([d]) => d),
      });
    }
  } catch (err) {
    self.postMessage({ type: 'error', id, message: err.message });
  }
};
