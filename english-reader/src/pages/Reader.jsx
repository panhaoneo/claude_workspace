import React, { useState, useEffect } from 'react';
import { useParams, useNavigate } from 'react-router-dom';
import { getBook, saveProgress, getProgress } from '../utils/db.js';
import { useApp } from '../App.jsx';
import EpubReader from '../components/EpubReader.jsx';
import PdfReader from '../components/PdfReader.jsx';
import WebReader from '../components/WebReader.jsx';

export default function Reader() {
  const { bookId } = useParams();
  const navigate = useNavigate();
  const { cefrLevel, apiConfig, fontSize, setFontSize, theme, setTheme } = useApp();
  const [book, setBook] = useState(null);
  const [loading, setLoading] = useState(true);
  const [showToolbar, setShowToolbar] = useState(true);
  const [annotationStatus, setAnnotationStatus] = useState(null);
  const [showSettings, setShowSettings] = useState(false);

  useEffect(() => {
    loadBook();
  }, [bookId]);

  // Auto-hide toolbar after 3s
  useEffect(() => {
    if (showToolbar) {
      const t = setTimeout(() => setShowToolbar(false), 4000);
      return () => clearTimeout(t);
    }
  }, [showToolbar]);

  async function loadBook() {
    const b = await getBook(parseInt(bookId));
    setBook(b);
    setLoading(false);
  }

  function handleAnnotationProgress(status) {
    setAnnotationStatus(status);
  }

  if (loading) {
    return (
      <div className="reader-container">
        <div className="loading-overlay">
          <div className="big-spinner" />
          <div className="loading-text">加载中...</div>
        </div>
      </div>
    );
  }

  if (!book) {
    return (
      <div className="reader-container">
        <div style={{padding:20,textAlign:'center',marginTop:40}}>
          <p style={{color:'var(--text-secondary)'}}>书籍不存在</p>
          <button className="btn btn-primary" style={{marginTop:12}} onClick={() => navigate('/')}>返回书架</button>
        </div>
      </div>
    );
  }

  return (
    <div className="reader-container" onClick={() => setShowToolbar(!showToolbar)}>
      {/* Header */}
      {showToolbar && (
        <div className="reader-header" onClick={e => e.stopPropagation()}>
          <button className="icon-btn" onClick={() => navigate('/')}>
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <path d="M19 12H5M12 19l-7-7 7-7"/>
            </svg>
          </button>
          <h2>{book.title}</h2>
          {annotationStatus?.status === 'loading' && (
            <div className="annotation-status loading">
              <div className="spinner" />
              <span>标注中 {annotationStatus.done || 0}/{annotationStatus.total || '?'}</span>
            </div>
          )}
          {annotationStatus?.status === 'done' && (
            <div className="annotation-status done">✓ 已标注</div>
          )}
          <button className="icon-btn" onClick={() => setShowSettings(!showSettings)}>
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <circle cx="12" cy="12" r="1"/><circle cx="19" cy="12" r="1"/><circle cx="5" cy="12" r="1"/>
            </svg>
          </button>
        </div>
      )}

      {/* Reader Content */}
      <div className="reader-content" style={{fontSize: `${fontSize}px`}}>
        {book.format === 'epub' && (
          <EpubReader
            book={book}
            cefrLevel={cefrLevel}
            apiConfig={apiConfig}
            fontSize={fontSize}
            onAnnotationProgress={handleAnnotationProgress}
          />
        )}
        {book.format === 'pdf' && (
          <PdfReader
            book={book}
            cefrLevel={cefrLevel}
            apiConfig={apiConfig}
            fontSize={fontSize}
            onAnnotationProgress={handleAnnotationProgress}
          />
        )}
        {book.format === 'web' && (
          <WebReader
            book={book}
            cefrLevel={cefrLevel}
            apiConfig={apiConfig}
            fontSize={fontSize}
            onAnnotationProgress={handleAnnotationProgress}
          />
        )}
      </div>

      {/* Bottom toolbar */}
      {showToolbar && (
        <div className="reader-toolbar" onClick={e => e.stopPropagation()}>
          <div className="reader-toolbar-group">
            <button className="icon-btn" onClick={() => setFontSize(Math.max(14, fontSize - 1))}>
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                <path d="M4 6h16M4 12h16M4 18h7"/><line x1="16" y1="18" x2="22" y2="18"/>
              </svg>
            </button>
            <span className="font-size-display">{fontSize}</span>
            <button className="icon-btn" onClick={() => setFontSize(Math.min(30, fontSize + 1))}>
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                <path d="M4 6h16M4 12h16M4 18h7"/><line x1="19" y1="15" x2="19" y2="21"/><line x1="16" y1="18" x2="22" y2="18"/>
              </svg>
            </button>
          </div>

          <div className="reader-toolbar-group">
            {['light','sepia','dark'].map(t => (
              <button
                key={t}
                onClick={() => setTheme(t)}
                style={{
                  width:28,height:28,borderRadius:'50%',border:`2px solid ${theme===t?'var(--primary)':'var(--border)'}`,
                  background: t==='light'?'#fff':t==='sepia'?'#f8f3e8':'#1a1a1a',
                  cursor:'pointer',margin:'0 3px'
                }}
              />
            ))}
          </div>
        </div>
      )}

      {/* Settings panel */}
      {showSettings && (
        <div
          style={{position:'fixed',inset:0,background:'rgba(0,0,0,0.3)',zIndex:500}}
          onClick={() => setShowSettings(false)}
        >
          <div
            style={{position:'absolute',top:48,right:8,background:'var(--surface)',borderRadius:12,padding:16,boxShadow:'0 4px 16px rgba(0,0,0,0.15)',minWidth:200}}
            onClick={e => e.stopPropagation()}
          >
            <div style={{fontSize:13,fontWeight:600,color:'var(--text-secondary)',marginBottom:8}}>阅读设置</div>
            <div style={{fontSize:13,marginBottom:8}}>
              当前 CEFR 等级：<strong>{cefrLevel}</strong>
            </div>
            <div style={{fontSize:12,color:'var(--text-secondary)'}}>
              {apiConfig?.apiKey ? '✓ API 已配置' : '⚠ 未配置 API，无法标注'}
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
