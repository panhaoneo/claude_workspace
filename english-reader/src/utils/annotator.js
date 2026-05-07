import { getWordLevel, isBasicWord, getLevelIndex, isWordAboveLevel } from './cefrWords.js';
import { getCachedWords, bulkCacheTranslations } from './db.js';
import { analyzeWords, chunkArray } from './api.js';

const WORD_REGEX = /\b([a-zA-Z]{2,}(?:'[a-zA-Z]+)?)\b/g;

// Extract unique words from a DOM element (skips ruby rt elements)
function extractTextWords(element) {
  const words = new Set();
  const walker = document.createTreeWalker(
    element,
    NodeFilter.SHOW_TEXT,
    {
      acceptNode(node) {
        const p = node.parentElement;
        if (!p) return NodeFilter.FILTER_REJECT;
        const tag = p.tagName?.toUpperCase();
        // Skip annotation elements
        if (tag === 'RT' || tag === 'SCRIPT' || tag === 'STYLE') return NodeFilter.FILTER_REJECT;
        // Skip if already annotated
        if (p.closest && p.closest('ruby')) return NodeFilter.FILTER_REJECT;
        return NodeFilter.FILTER_ACCEPT;
      }
    }
  );
  let node;
  while ((node = walker.nextNode())) {
    const matches = node.textContent.matchAll(WORD_REGEX);
    for (const m of matches) {
      words.add(m[1].toLowerCase());
    }
  }
  return [...words];
}

// Determine which words need translation given user CEFR level
function wordsNeedingAnnotation(words, userLevel, cachedTranslations) {
  return words.filter(word => {
    // Skip very short words
    if (word.length < 3) return false;
    // Skip numbers
    if (/^\d+$/.test(word)) return false;
    // Basic A1/A2 words never need annotation if user is A2+
    if (isBasicWord(word) && getLevelIndex(userLevel) >= getLevelIndex('A2')) return false;

    const cached = cachedTranslations[word];
    if (cached) {
      return isWordAboveLevel(cached.level, userLevel);
    }
    // No cache — assume needs annotation if not basic
    return !isBasicWord(word);
  });
}

// Apply ruby annotations to DOM
function applyAnnotationsToElement(element, translations, userLevel) {
  const walker = document.createTreeWalker(
    element,
    NodeFilter.SHOW_TEXT,
    {
      acceptNode(node) {
        const p = node.parentElement;
        if (!p) return NodeFilter.FILTER_REJECT;
        const tag = p.tagName?.toUpperCase();
        if (tag === 'RT' || tag === 'SCRIPT' || tag === 'STYLE') return NodeFilter.FILTER_REJECT;
        if (p.closest && p.closest('ruby')) return NodeFilter.FILTER_REJECT;
        return NodeFilter.FILTER_ACCEPT;
      }
    }
  );

  const textNodes = [];
  let node;
  while ((node = walker.nextNode())) {
    textNodes.push(node);
  }

  for (const textNode of textNodes) {
    const text = textNode.textContent;
    let lastIndex = 0;
    const fragment = document.createDocumentFragment();
    let modified = false;

    const re = new RegExp(WORD_REGEX.source, 'g');
    let match;
    while ((match = re.exec(text)) !== null) {
      const word = match[1];
      const wordLower = word.toLowerCase();
      const cached = translations[wordLower];

      if (!cached || !isWordAboveLevel(cached.level, userLevel)) continue;

      modified = true;
      // Text before this word
      if (match.index > lastIndex) {
        fragment.appendChild(document.createTextNode(text.slice(lastIndex, match.index)));
      }

      // Create ruby annotation
      const ruby = document.createElement('ruby');
      ruby.style.cssText = 'display:ruby;ruby-align:center;';
      ruby.dataset.word = wordLower;
      ruby.dataset.level = cached.level;

      const wordSpan = document.createTextNode(word);
      ruby.appendChild(wordSpan);

      const rt = document.createElement('rt');
      rt.textContent = cached.zh;
      rt.style.cssText = `
        font-size:0.6em;
        color:#d44;
        line-height:1.2;
        font-style:normal;
        font-weight:400;
        font-family:-apple-system,sans-serif;
        letter-spacing:0;
      `;
      ruby.appendChild(rt);
      fragment.appendChild(ruby);

      lastIndex = match.index + word.length;
    }

    if (modified) {
      if (lastIndex < text.length) {
        fragment.appendChild(document.createTextNode(text.slice(lastIndex)));
      }
      textNode.parentNode.replaceChild(fragment, textNode);
    }
  }
}

// Main annotation function
export async function annotateElement(element, userLevel, apiConfig, onProgress) {
  if (!element) return;

  const allWords = extractTextWords(element);
  if (allWords.length === 0) return;

  // Check cache first
  const cached = await getCachedWords(allWords);

  // Figure out what needs annotation
  const toFetch = wordsNeedingAnnotation(allWords, userLevel, cached);

  // Split: already cached vs needs API
  const uncachedToFetch = toFetch.filter(w => !cached[w]);

  let allTranslations = { ...cached };

  // Fetch uncached words from API
  if (uncachedToFetch.length > 0 && apiConfig?.apiKey) {
    const batches = chunkArray(uncachedToFetch, 60);
    let done = 0;
    for (const batch of batches) {
      try {
        onProgress?.({ status: 'loading', done, total: uncachedToFetch.length });
        const results = await analyzeWords(batch, apiConfig);
        if (results.length > 0) {
          await bulkCacheTranslations(results);
          for (const r of results) {
            allTranslations[r.word.toLowerCase()] = r;
          }
        }
        done += batch.length;
      } catch (e) {
        console.warn('Annotation API error:', e);
        // Continue with what we have
      }
    }
  }

  // Apply annotations to DOM
  applyAnnotationsToElement(element, allTranslations, userLevel);
  onProgress?.({ status: 'done' });
}

// Get word info for tooltip (from cached data)
export async function getWordInfo(word) {
  const cached = await getCachedWords([word.toLowerCase()]);
  return cached[word.toLowerCase()] || null;
}
