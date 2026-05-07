import React, { useEffect, useRef, useState } from 'react';
import { annotateElement } from '../utils/annotator.js';
import { getWordInfo } from '../utils/annotator.js';

const CORS_PROXIES = [
  (url) => `https://api.allorigins.win/raw?url=${encodeURIComponent(url)}`,
  (url) => `https://corsproxy.io/?${encodeURIComponent(url)}`
];

export default function WebReader({ book, cefrLevel, apiConfig, fontSize, onAnnotationProgress }) {
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);
  const [article, setArticle] = useState(null); // { title, content (html string) }
  const [tooltip, setTooltip] = useState(null);
  const contentRef = useRef();
  const annotatedRef = useRef(false);

  useEffect(() => {
    fetchAndParse();
  }, [book.url]);

  useEffect(() => {
    if (article && contentRef.current && !annotatedRef.current) {
      annotatedRef.current = true;
      runAnnotation();
    }
  }, [article, cefrLevel]);

  async function fetchAndParse() {
    const url = book.url;

    // If URL is a PDF, suggest PDF reader
    if (url.match(/\.pdf(\?|$)/i)) {
      setError('此链接是 PDF 文件，请在书架中将格式改为 PDF 打开。');
      setLoading(false);
      return;
    }

    let html = null;
    let fetchError = null;

    // Try direct fetch first
    try {
      const res = await fetch(url, { mode: 'cors' });
      if (res.ok) html = await res.text();
    } catch (e) {
      fetchError = e;
    }

    // Try CORS proxies
    if (!html) {
      for (const makeProxy of CORS_PROXIES) {
        try {
          const proxyUrl = makeProxy(url);
          const res = await fetch(proxyUrl);
          if (res.ok) {
            html = await res.text();
            break;
          }
        } catch {}
      }
    }

    if (!html) {
      setError(`无法加载页面。可能是 CORS 限制。\n原始错误: ${fetchError?.message || '未知'}`);
      setLoading(false);
      return;
    }

    try {
      const parsed = parseArticle(html, url);
      setArticle(parsed);
    } catch (e) {
      setError('页面解析失败: ' + e.message);
    }
    setLoading(false);
  }

  function parseArticle(html, baseUrl) {
    const parser = new DOMParser();
    const doc = parser.parseFromString(html, 'text/html');

    // Fix relative URLs
    doc.querySelectorAll('[href]').forEach(el => {
      try { el.href = new URL(el.getAttribute('href'), baseUrl).href; } catch {}
    });

    // Get title
    const title =
      doc.querySelector('h1')?.textContent?.trim() ||
      doc.querySelector('title')?.textContent?.trim() ||
      book.title;

    // Try to extract main content using heuristics
    const mainSelectors = [
      'article',
      'main',
      '[role="main"]',
      '.article-content',
      '.post-content',
      '.entry-content',
      '#content',
      '.content',
      '.abstract',
      '#abs'
    ];

    let mainEl = null;
    for (const sel of mainSelectors) {
      mainEl = doc.querySelector(sel);
      if (mainEl) break;
    }

    if (!mainEl) {
      mainEl = doc.body;
    }

    // Remove noise elements
    ['script','style','nav','header','footer','aside','.sidebar','.advertisement','.ads','.share','.social','.comments'].forEach(sel => {
      mainEl?.querySelectorAll(sel).forEach(el => el.remove());
    });

    // Extract meaningful paragraphs
    const paragraphs = [];
    const els = mainEl?.querySelectorAll('p, h1, h2, h3, h4, blockquote, li') || [];

    for (const el of els) {
      const text = el.textContent?.trim();
      if (text && text.length > 20) {
        paragraphs.push({
          tag: el.tagName.toLowerCase(),
          text
        });
      }
    }

    // If no paragraphs found, fall back to full body text
    if (paragraphs.length === 0) {
      const bodyText = mainEl?.textContent || '';
      bodyText.split('\n').filter(l => l.trim().length > 30).forEach(line => {
        paragraphs.push({ tag: 'p', text: line.trim() });
      });
    }

    return { title, paragraphs };
  }

  async function runAnnotation() {
    if (!contentRef.current) return;
    try {
      await annotateElement(contentRef.current, cefrLevel, apiConfig, onAnnotationProgress);
    } catch (e) {
      console.warn('Web annotation error:', e);
    }
  }

  async function handleClick(e) {
    const ruby = e.target.closest('ruby[data-word]');
    if (!ruby) { setTooltip(null); return; }
    e.stopPropagation();
    const word = ruby.dataset.word;
    const info = await getWordInfo(word);
    if (info) setTooltip({ word, ...info });
  }

  if (loading) {
    return (
      <div className="loading-overlay">
        <div className="big-spinner" />
        <div className="loading-text">加载网页...</div>
      </div>
    );
  }

  if (error) {
    return (
      <div className="error-state" style={{padding:24}}>
        <p style={{fontSize:16,fontWeight:600}}>加载失败</p>
        <p style={{marginTop:8,fontSize:13,color:'var(--text-secondary)',whiteSpace:'pre-wrap',wordBreak:'break-all'}}>{error}</p>
        <p style={{marginTop:12,fontSize:13,color:'var(--text-secondary)'}}>
          建议：下载文件后以 PDF 格式导入，或将网页内容复制后以文本形式使用。
        </p>
      </div>
    );
  }

  return (
    <div
      className="web-reader"
      style={{position:'relative'}}
      onClick={handleClick}
    >
      <div ref={contentRef} className="web-reader-content">
        {article?.title && (
          <h1 className="web-reader-title">{article.title}</h1>
        )}
        {article?.paragraphs?.map((p, i) => {
          const Tag = ['h1','h2','h3','h4'].includes(p.tag) ? p.tag : 'p';
          const isHeading = ['h1','h2','h3','h4'].includes(p.tag);
          return (
            <Tag
              key={i}
              style={{
                fontSize: isHeading ? `${Math.round(fontSize * 1.2)}px` : `${fontSize}px`,
                fontWeight: isHeading ? 700 : 400,
                lineHeight: 1.8,
                marginBottom: isHeading ? '0.8em' : '1em',
                marginTop: isHeading ? '1.2em' : 0,
                color: 'var(--text)',
                fontFamily: isHeading ? '-apple-system,sans-serif' : 'Georgia,serif'
              }}
            >
              {p.text}
            </Tag>
          );
        })}
      </div>

      {tooltip && (
        <div className="word-tooltip" onClick={e => { e.stopPropagation(); setTooltip(null); }}>
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
