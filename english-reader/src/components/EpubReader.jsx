import React, { useEffect, useRef, useState, useCallback } from 'react';
import { annotateElement } from '../utils/annotator.js';
import { getWordInfo } from '../utils/annotator.js';

export default function EpubReader({ book, cefrLevel, apiConfig, fontSize, onAnnotationProgress }) {
  const containerRef = useRef();
  const renditionRef = useRef();
  const bookRef = useRef();
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);
  const [tooltip, setTooltip] = useState(null);
  const [currentPage, setCurrentPage] = useState('');

  useEffect(() => {
    initEpub();
    return () => {
      bookRef.current?.destroy();
    };
  }, []);

  useEffect(() => {
    if (renditionRef.current) {
      renditionRef.current.themes.fontSize(`${fontSize}px`);
    }
  }, [fontSize]);

  async function initEpub() {
    try {
      const ePub = (await import('epubjs')).default;

      let bookData;
      if (book.data) {
        bookData = book.data;
      } else if (book.url) {
        const res = await fetch(book.url);
        if (!res.ok) throw new Error(`Failed to fetch: ${res.status}`);
        bookData = await res.arrayBuffer();
      }

      const epubBook = ePub(bookData);
      bookRef.current = epubBook;

      await epubBook.ready;

      if (!containerRef.current) return;

      const rendition = epubBook.renderTo(containerRef.current, {
        width: '100%',
        height: '100%',
        flow: 'scrolled-doc',
        spread: 'none',
        manager: 'continuous'
      });
      renditionRef.current = rendition;

      rendition.themes.fontSize(`${fontSize}px`);
      rendition.themes.default({
        body: {
          'font-family': '-apple-system, Georgia, serif !important',
          'line-height': '1.8 !important',
          'padding': '0 16px !important',
          'max-width': '680px !important',
          'margin': '0 auto !important',
          'color': 'var(--text, #1a1a1a) !important'
        },
        p: { 'margin-bottom': '1em !important' },
        ruby: { 'display': 'ruby !important' },
        rt: { 'font-size': '0.6em !important', 'color': '#d44 !important' }
      });

      // Hook: annotate after each section renders
      rendition.hooks.content.register(async (contents) => {
        const doc = contents.document;
        if (!doc?.body) return;

        // Small delay to let layout settle
        await new Promise(r => setTimeout(r, 100));

        try {
          await annotateElement(doc.body, cefrLevel, apiConfig, onAnnotationProgress);
        } catch (e) {
          console.warn('Annotation failed:', e);
        }

        // Add word tap handler
        doc.addEventListener('click', handleWordClick);
      });

      rendition.on('relocated', (location) => {
        setCurrentPage(location?.start?.cfi || '');
      });

      await rendition.display();
      setLoading(false);
    } catch (e) {
      console.error('EPUB error:', e);
      setError(e.message);
      setLoading(false);
    }
  }

  async function handleWordClick(e) {
    const ruby = e.target.closest('ruby[data-word]');
    if (!ruby) {
      setTooltip(null);
      return;
    }
    e.stopPropagation();
    const word = ruby.dataset.word;
    const info = await getWordInfo(word);
    if (info) {
      setTooltip({ word, ...info });
    }
  }

  if (error) {
    return (
      <div className="error-state" style={{padding:24}}>
        <p style={{fontSize:16,fontWeight:600}}>加载失败</p>
        <p style={{marginTop:8,color:'var(--text-secondary)',wordBreak:'break-all'}}>{error}</p>
        <p style={{marginTop:8,fontSize:13,color:'var(--text-secondary)'}}>
          如果是网络问题，可以先下载 epub 文件再导入
        </p>
      </div>
    );
  }

  return (
    <div style={{width:'100%',height:'100%',position:'relative'}}>
      {loading && (
        <div className="loading-overlay">
          <div className="big-spinner" />
          <div className="loading-text">加载 EPUB...</div>
        </div>
      )}

      <div
        ref={containerRef}
        className="epub-area"
        style={{width:'100%',height:'100%'}}
      />

      {tooltip && (
        <div
          className="word-tooltip"
          onClick={e => { e.stopPropagation(); setTooltip(null); }}
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
