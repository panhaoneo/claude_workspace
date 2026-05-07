import React, { useEffect, useRef, useState } from 'react';
import { getCachedWords, bulkCacheTranslations } from '../utils/db.js';
import { analyzeWords, chunkArray } from '../utils/api.js';
import { isBasicWord, getLevelIndex, isWordAboveLevel } from '../utils/cefrWords.js';
import { getWordInfo } from '../utils/annotator.js';

export default function PdfReader({ book, cefrLevel, apiConfig, fontSize, onAnnotationProgress }) {
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);
  const [pages, setPages] = useState([]); // [{text, items}]
  const [numPages, setNumPages] = useState(0);
  const [renderedPages, setRenderedPages] = useState(1);
  const [translations, setTranslations] = useState({});
  const [tooltip, setTooltip] = useState(null);
  const containerRef = useRef();
  const pdfRef = useRef();
  const observerRef = useRef();

  useEffect(() => {
    loadPdf();
  }, []);

  // Lazy load more pages as user scrolls
  useEffect(() => {
    if (!containerRef.current || renderedPages >= numPages) return;
    const observer = new IntersectionObserver(
      (entries) => {
        if (entries[0].isIntersecting && renderedPages < numPages) {
          setRenderedPages(p => Math.min(p + 3, numPages));
        }
      },
      { threshold: 0.5 }
    );
    const sentinel = containerRef.current.querySelector('.pdf-sentinel');
    if (sentinel) observer.observe(sentinel);
    observerRef.current = observer;
    return () => observer.disconnect();
  }, [renderedPages, numPages]);

  async function loadPdf() {
    try {
      const pdfjsLib = await import('pdfjs-dist');
      // Use CDN worker to avoid bundling issues
      pdfjsLib.GlobalWorkerOptions.workerSrc =
        `https://cdnjs.cloudflare.com/ajax/libs/pdf.js/${pdfjsLib.version}/pdf.worker.min.mjs`;

      let source;
      if (book.data) {
        source = { data: new Uint8Array(book.data) };
      } else if (book.url) {
        source = { url: book.url, withCredentials: false };
      }

      const pdf = await pdfjsLib.getDocument({
        ...source,
        cMapUrl: `https://cdn.jsdelivr.net/npm/pdfjs-dist@${pdfjsLib.version}/cmaps/`,
        cMapPacked: true
      }).promise;

      pdfRef.current = pdf;
      setNumPages(pdf.numPages);

      // Extract text from first batch of pages
      const pageTexts = [];
      const firstBatch = Math.min(5, pdf.numPages);
      for (let i = 1; i <= firstBatch; i++) {
        const text = await extractPageText(pdf, i);
        pageTexts.push(text);
      }
      setPages(pageTexts);
      setRenderedPages(firstBatch);
      setLoading(false);

      // Start annotation
      await annotatePages(pageTexts);

      // Load remaining pages in background
      if (pdf.numPages > firstBatch) {
        loadRemainingPages(pdf, firstBatch + 1);
      }
    } catch (e) {
      console.error('PDF error:', e);
      setError(e.message);
      setLoading(false);
    }
  }

  async function extractPageText(pdf, pageNum) {
    const page = await pdf.getPage(pageNum);
    const content = await page.getTextContent();
    const items = content.items
      .filter(item => item.str?.trim())
      .map(item => ({ str: item.str, x: item.transform[4], y: item.transform[5] }));

    // Group items into paragraphs by y-position
    const lines = [];
    let currentLine = [];
    let lastY = null;
    for (const item of items) {
      if (lastY !== null && Math.abs(item.y - lastY) > 5) {
        if (currentLine.length) lines.push(currentLine.join(' '));
        currentLine = [];
      }
      currentLine.push(item.str);
      lastY = item.y;
    }
    if (currentLine.length) lines.push(currentLine.join(' '));

    return { pageNum, lines, viewport: page.getViewport({ scale: 1 }) };
  }

  async function loadRemainingPages(pdf, startPage) {
    const newPages = [];
    for (let i = startPage; i <= pdf.numPages; i++) {
      const text = await extractPageText(pdf, i);
      newPages.push(text);
    }
    setPages(prev => {
      const all = [...prev, ...newPages];
      annotatePages(newPages);
      return all;
    });
    setNumPages(pdf.numPages);
  }

  async function annotatePages(pageList) {
    if (!apiConfig?.apiKey) return;

    // Collect all unique words
    const allWords = new Set();
    for (const page of pageList) {
      for (const line of page.lines) {
        const matches = line.matchAll(/\b([a-zA-Z]{3,})\b/g);
        for (const m of matches) {
          allWords.add(m[1].toLowerCase());
        }
      }
    }

    const wordList = [...allWords].filter(w => !isBasicWord(w));
    if (wordList.length === 0) return;

    // Check cache
    const cached = await getCachedWords(wordList);
    const uncached = wordList.filter(w => !cached[w]);

    const newTranslations = { ...cached };

    if (uncached.length > 0) {
      const batches = chunkArray(uncached, 60);
      let done = 0;
      for (const batch of batches) {
        onAnnotationProgress?.({ status: 'loading', done, total: uncached.length });
        try {
          const results = await analyzeWords(batch, apiConfig);
          await bulkCacheTranslations(results);
          for (const r of results) newTranslations[r.word.toLowerCase()] = r;
          done += batch.length;
        } catch (e) {
          console.warn('Annotation batch error:', e);
        }
      }
    }

    setTranslations(newTranslations);
    onAnnotationProgress?.({ status: 'done' });
  }

  function renderAnnotatedText(text) {
    const parts = [];
    let lastIndex = 0;
    const re = /\b([a-zA-Z]{2,}(?:'[a-zA-Z]+)?)\b/g;
    let match;
    while ((match = re.exec(text)) !== null) {
      const word = match[1];
      const lower = word.toLowerCase();
      const trans = translations[lower];
      if (trans && isWordAboveLevel(trans.level, cefrLevel)) {
        if (match.index > lastIndex) {
          parts.push(text.slice(lastIndex, match.index));
        }
        parts.push(
          <ruby
            key={`${lower}-${match.index}`}
            data-word={lower}
            style={{display:'ruby',cursor:'pointer'}}
            onClick={(e) => { e.stopPropagation(); handleWordTap(lower); }}
          >
            {word}
            <rt style={{fontSize:'0.6em',color:'#d44',lineHeight:1.2}}>{trans.zh}</rt>
          </ruby>
        );
        lastIndex = match.index + word.length;
      }
    }
    if (lastIndex < text.length) parts.push(text.slice(lastIndex));
    return parts;
  }

  async function handleWordTap(word) {
    const info = translations[word] || await getWordInfo(word);
    if (info) setTooltip({ word, ...info });
  }

  if (error) {
    return (
      <div className="error-state" style={{padding:24}}>
        <p style={{fontSize:16,fontWeight:600}}>PDF 加载失败</p>
        <p style={{marginTop:8,color:'var(--text-secondary)',wordBreak:'break-all',fontSize:13}}>{error}</p>
        <p style={{marginTop:8,fontSize:13,color:'var(--text-secondary)'}}>
          部分 PDF 可能因 CORS 限制无法直接加载。可以先下载文件再导入。
        </p>
      </div>
    );
  }

  return (
    <div ref={containerRef} className="pdf-reader" style={{position:'relative'}}>
      {loading && (
        <div className="loading-overlay">
          <div className="big-spinner" />
          <div className="loading-text">加载 PDF...</div>
        </div>
      )}

      {pages.map((page, pi) => (
        <div key={page.pageNum} style={{
          background:'var(--surface)',
          margin:'8px 12px',
          borderRadius:8,
          padding:'16px',
          boxShadow:'0 1px 4px rgba(0,0,0,0.1)'
        }}>
          <div style={{
            textAlign:'center',
            fontSize:11,
            color:'var(--text-secondary)',
            marginBottom:12,
            borderBottom:'1px solid var(--border)',
            paddingBottom:8
          }}>
            第 {page.pageNum} 页 / 共 {numPages} 页
          </div>
          {page.lines.map((line, li) => (
            <p key={li} style={{
              marginBottom:'0.6em',
              lineHeight:1.8,
              fontSize: `${fontSize}px`,
              color:'var(--text)',
              fontFamily:'Georgia, serif'
            }}>
              {renderAnnotatedText(line)}
            </p>
          ))}
        </div>
      ))}

      {numPages > renderedPages && (
        <div className="pdf-sentinel" style={{height:20,marginBottom:8}} />
      )}

      {numPages > 0 && renderedPages >= numPages && (
        <div style={{textAlign:'center',padding:20,color:'var(--text-secondary)',fontSize:14}}>
          — 全文结束 —
        </div>
      )}

      {tooltip && (
        <div
          className="word-tooltip"
          onClick={() => setTooltip(null)}
        >
          <div className="word-tooltip-word">{tooltip.word}</div>
          <div className={`word-tooltip-level level-color-${tooltip.level?.toLowerCase()}`}>
            {tooltip.level}
          </div>
          <div className="word-tooltip-zh">{tooltip.zh}</div>
          <div style={{fontSize:12,color:'var(--text-secondary)',marginTop:6}}>点击关闭</div>
        </div>
      )}
    </div>
  );
}
