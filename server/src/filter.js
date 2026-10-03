

const SWAPS = {
  "0": "o", "1": "i", "!": "i", "|": "i", "3": "e", "4": "a", "@": "a", "5": "s", "$": "s",
  "7": "t", "+": "t", "8": "b", "9": "g", "6": "g", "2": "z",
};

const ROOTS = [
  "fuck", "fuk", "fck", "shit", "cunt", "nigg", "niga", "nigga", "faggot", "fagot", "retard",
  "bitch", "whore", "slut", "dildo", "jizz", "wank", "rapist", "pussy", "motherf", "asshole",
  "bastard", "kike", "tranny", "penis", "vagina", "porn", "boner", "titty", "titties", "blowjob",
  "handjob", "cumshot", "dickhead", "douche", "chode", "twat", "spastic",
];

const WORDS = new Set([
  "ass", "asses", "arse", "arses", "dick", "dicks", "cock", "cocks", "cum", "cums", "coon",
  "coons", "fag", "fags", "hoe", "hoes", "tit", "tits", "piss", "pissed", "prick", "pricks",
  "rape", "raped", "raping", "spic", "spics", "chink", "chinks", "dyke", "dykes", "kys",
  "nazi", "nazis", "wtf", "stfu", "gtfo", "boob", "boobs", "butthole", "anal", "sex", "sexy",
  "nude", "nudes", "horny", "cuck", "simp", "bollocks", "bugger",
].map((w) => squeeze(w)));

const ALLOW = new Set([
  "therapist", "therapists", "scunthorpe", "shitake", "shiitake", "niggle", "niggles", "niggled",
  "niggling", "snigger", "sniggers", "sniggered", "sniggering", "penistone", "matsushita",
  "cockpit", "hancock", "dickens", "dickinson", "titmouse", "bitchute",
].map((w) => squeeze(w)));

const PHRASES = ["killyourself", "killurself", "kilyourself", "goddie", "hangyourself"];

function squeeze(word) {
  return word.replace(/(.)\1+/g, "$1");
}

function plain(word) {
  let out = "";
  for (const c of word.toLowerCase()) out += SWAPS[c] || c;
  return squeeze(out.replace(/[^a-z]/g, ""));
}

function bad(word) {
  const p = plain(word);
  if (p.length === 0 || ALLOW.has(p)) return false;
  if (WORDS.has(p)) return true;
  for (const root of ROOTS) {
    if (p.includes(squeeze(root))) return true;
  }
  return false;
}

const stars = (s) => "*".repeat(s.length);

export function cleanChat(text) {

  const parts = text.split(/(\s+)/);
  const words = parts.map((p, i) => ({ p, i, space: /^\s+$/.test(p) }));
  const out = parts.slice();

  for (const w of words) {
    if (!w.space && bad(w.p)) out[w.i] = stars(w.p);
  }

  const letters = words.filter((w) => !w.space);
  for (let start = 0; start < letters.length; ) {
    let end = start;
    while (end < letters.length && plain(letters[end].p).length === 1) ++end;
    if (end - start >= 3 && bad(letters.slice(start, end).map((w) => w.p).join(""))) {
      for (let k = start; k < end; ++k) out[letters[k].i] = stars(letters[k].p);
    }
    start = end > start ? end : start + 1;
  }
  for (const w of letters) {

    if (/^[a-z0-9@$!|]([.\-_*][a-z0-9@$!|]){2,}$/i.test(w.p) && bad(w.p)) out[w.i] = stars(w.p);
  }

  let result = out.join("");
  const flat = plain(result);
  for (const phrase of PHRASES) {
    if (!flat.includes(squeeze(phrase))) continue;

    result = result.replace(/\b(kill|kil|hang|go)\b(\s*)(\w+)(\s*)(self|yourself|urself|die)?/gi,
      (m) => stars(m));
  }
  return result;
}

export function cleanName(name) {
  const cleaned = cleanChat(name);
  return cleaned.includes("*") ? "" : cleaned;
}
