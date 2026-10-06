

import {
  DataSet, RegExpMatcher, englishDataset, englishRecommendedTransformers, pattern,
} from "obscenity";

const WHITELIST = ["cockpit", "cockpits", "penistone", "hancock", "dickens", "dickinson"];

const dataset = new DataSet()
  .addAll(englishDataset)
  .addPhrase((p) => p.setMetadata({ originalWord: "kys" }).addPattern(pattern`|kys|`))
  .addPhrase((p) => p.setMetadata({ originalWord: "spic" }).addPattern(pattern`|spic|`)
    .addPattern(pattern`|spics|`));

const matcher = new RegExpMatcher({
  ...dataset.build(),
  ...englishRecommendedTransformers,
  whitelistedTerms: [...dataset.build().whitelistedTerms, ...WHITELIST],
});

const PHRASES = ["killyourself", "killurself", "kilyourself", "goddie", "hangyourself"];

const stars = (s) => "*".repeat(s.length);
const flat = (s) => s.toLowerCase().replace(/[^a-z]/g, "").replace(/(.)\1+/g, "$1");

export function cleanChat(text) {
  const out = text.split("");
  const starRange = (from, to) => {
    for (let i = from; i < to; ++i) if (!/\s/.test(out[i])) out[i] = "*";
  };

  for (const m of matcher.getAllMatches(text)) {
    let from = m.startIndex;
    let to = m.endIndex + 1;
    while (from > 0 && !/\s/.test(text[from - 1])) --from;
    while (to < text.length && !/\s/.test(text[to])) ++to;
    starRange(from, to);
  }

  const words = [];
  for (const m of text.matchAll(/\S+/g)) words.push({ w: m[0], at: m.index });
  const letters = (w) => w.replace(/[^a-z0-9@$!|]/gi, "");

  for (let start = 0; start < words.length; ) {
    let end = start;
    while (end < words.length && letters(words[end].w).length === 1) ++end;
    if (end - start >= 3 && matcher.hasMatch(words.slice(start, end).map((x) => x.w).join(""))) {
      starRange(words[start].at, words[end - 1].at + words[end - 1].w.length);
    }
    start = end > start ? end : start + 1;
  }

  for (const { w, at } of words) {
    if (/^[a-z0-9@$!|]([.\-_*][a-z0-9@$!|]){2,}$/i.test(w) && matcher.hasMatch(letters(w))) {
      starRange(at, at + w.length);
    }
  }

  let result = out.join("");
  const whole = flat(result);
  if (PHRASES.some((p) => whole.includes(flat(p)))) {

    result = result.replace(/\b(k+i+l+|h+a+n+g+|g+o+)\b(\s*)(\w+)(\s*)(self|yourself|urself|die)?/gi,
      (m) => m.replace(/\S/g, "*"));
  }
  return result;
}

export function cleanName(name) {
  const cleaned = cleanChat(name);
  return cleaned.includes("*") ? "" : cleaned;
}
