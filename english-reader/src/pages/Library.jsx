import React, { useState, useEffect, useRef } from 'react';
import { useNavigate } from 'react-router-dom';
import { getBooks, saveBook, deleteBook } from '../utils/db.js';

const COVER_COLORS = ['cover-0','cover-1','cover-2','cover-3','cover-4','cover-5'];
const SAMPLE_BOOKS = [
  {
    title: "Alice's Adventures in Wonderland",
    author: 'Lewis Carroll',
    format: 'epub',
    url: 'https://www.gutenberg.org/cache/epub/11/pg11.epub',
    isSample: true
  },
  {
    title: 'arXiv: Scaling LLM Test-Time Compute',
    author: 'arXiv 2412.20138',
    format: 'pdf',
    url: 'https://arxiv.org/pdf/2412.20138',
    isSample: true
  },
  {
    title: 'arXiv Paper (Web View)',
    author: 'arXiv 2412.20138',
    format: 'web',
    url: 'https://arxiv.org/abs/2412.20138',
    isSample: true
  }
];

export default function Library() {
  const [books, setBooks] = useState([]);
  const [showAdd, setShowAdd] = useState(false);
  const [showUrlForm, setShowUrlForm] = useState(false);
  const [urlInput, setUrlInput] = useState('');
  const [urlTitle, setUrlTitle] = useState('');
  const [urlFormat, setUrlFormat] = useState('auto');
  const [longPressBook, setLongPressBook] = useState(null);
  const fileInputRef = useRef();
  const navigate = useNavigate();

  useEffect(() => {
    loadBooks();
  }, []);

  async function loadBooks() {
    const list = await getBooks();
    setBooks(list);
  }

  async function handleFileUpload(e) {
    const file = e.target.files?.[0];
    if (!file) return;

    const format = file.name.endsWith('.epub') ? 'epub' : file.name.endsWith('.pdf') ? 'pdf' : null;
    if (!format) {
      alert('请选择 epub 或 pdf 文件');
      return;
    }

    const data = await file.arrayBuffer();
    await saveBook({
      title: file.name.replace(/\.(epub|pdf)$/i, ''),
      author: '本地文件',
      format,
      data,
      coverIndex: Math.floor(Math.random() * COVER_COLORS.length)
    });
    await loadBooks();
    setShowAdd(false);
  }

  async function handleAddUrl() {
    if (!urlInput.trim()) return;
    let url = urlInput.trim();
    if (!url.startsWith('http')) url = 'https://' + url;

    let fmt = urlFormat;
    if (fmt === 'auto') {
      if (url.match(/\.epub(\?|$)/i)) fmt = 'epub';
      else if (url.match(/\.pdf(\?|$)/i)) fmt = 'pdf';
      else fmt = 'web';
    }

    await saveBook({
      title: urlTitle.trim() || url,
      author: new URL(url).hostname,
      format: fmt,
      url,
      coverIndex: Math.floor(Math.random() * COVER_COLORS.length)
    });
    await loadBooks();
    setShowUrlForm(false);
    setUrlInput('');
    setUrlTitle('');
    setUrlFormat('auto');
    setShowAdd(false);
  }

  async function addSampleBook(sample) {
    await saveBook({
      ...sample,
      coverIndex: Math.floor(Math.random() * COVER_COLORS.length)
    });
    await loadBooks();
    setShowAdd(false);
  }

  async function handleDeleteBook(id) {
    if (!confirm('确定删除这本书？')) return;
    await deleteBook(id);
    setLongPressBook(null);
    await loadBooks();
  }

  let pressTimer;
  function handlePressStart(book) {
    pressTimer = setTimeout(() => setLongPressBook(book), 500);
  }
  function handlePressEnd() {
    clearTimeout(pressTimer);
  }

  return (
    <div className="page">
      <div className="library-header">
        <h1>书架</h1>
        <div className="library-actions">
          <button className="btn btn-primary" onClick={() => setShowAdd(true)}>
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" style={{width:16,height:16}}>
              <line x1="12" y1="5" x2="12" y2="19"/><line x1="5" y1="12" x2="19" y2="12"/>
            </svg>
            添加书籍
          </button>
        </div>
      </div>

      {books.length === 0 ? (
        <div className="empty-state">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5">
            <path d="M4 19.5A2.5 2.5 0 016.5 17H20"/>
            <path d="M6.5 2H20v20H6.5A2.5 2.5 0 014 19.5v-15A2.5 2.5 0 016.5 2z"/>
          </svg>
          <p style={{fontSize:16,fontWeight:600,color:'var(--text)'}}>书架还是空的</p>
          <p>点击「添加书籍」导入 EPUB、PDF 或网址</p>
          <button className="btn btn-primary" style={{marginTop:16}} onClick={() => setShowAdd(true)}>
            添加示例书籍
          </button>
        </div>
      ) : (
        <div className="book-list">
          {books.map(book => (
            <div
              key={book.id}
              className="book-card"
              onClick={() => navigate(`/reader/${book.id}`)}
              onTouchStart={() => handlePressStart(book)}
              onTouchEnd={handlePressEnd}
              onMouseDown={() => handlePressStart(book)}
              onMouseUp={handlePressEnd}
            >
              <div className={`book-cover ${COVER_COLORS[book.coverIndex ?? 0]}`}>
                <span style={{fontSize:28,zIndex:1}}>
                  {book.format === 'epub' ? '📖' : book.format === 'pdf' ? '📄' : '🌐'}
                </span>
                <span className="book-format-badge">{book.format}</span>
              </div>
              <div className="book-info">
                <div className="book-title">{book.title}</div>
                <div className="book-author">{book.author}</div>
                <div className="book-progress">
                  <div className="book-progress-fill" style={{width: '0%'}} />
                </div>
              </div>
            </div>
          ))}
        </div>
      )}

      {/* Long press menu */}
      {longPressBook && (
        <div className="modal-overlay" onClick={() => setLongPressBook(null)}>
          <div className="modal-sheet" onClick={e => e.stopPropagation()}>
            <div className="modal-handle" />
            <div className="modal-title">{longPressBook.title}</div>
            <div style={{padding:'0 0 8px'}}>
              <button
                style={{width:'100%',padding:'14px 20px',textAlign:'left',background:'none',border:'none',borderTop:'1px solid var(--border)',color:'#f44336',fontSize:15,cursor:'pointer'}}
                onClick={() => handleDeleteBook(longPressBook.id)}
              >
                删除
              </button>
              <button
                style={{width:'100%',padding:'14px 20px',textAlign:'left',background:'none',border:'none',borderTop:'1px solid var(--border)',color:'var(--text)',fontSize:15,cursor:'pointer'}}
                onClick={() => setLongPressBook(null)}
              >
                取消
              </button>
            </div>
          </div>
        </div>
      )}

      {/* Add book modal */}
      {showAdd && !showUrlForm && (
        <div className="modal-overlay" onClick={() => setShowAdd(false)}>
          <div className="modal-sheet" onClick={e => e.stopPropagation()}>
            <div className="modal-handle" />
            <div className="modal-title">添加书籍</div>

            <div style={{padding:'0 20px 16px',display:'flex',flexDirection:'column',gap:10}}>
              <button className="btn btn-secondary" style={{justifyContent:'flex-start',padding:'12px 16px',borderRadius:10}} onClick={() => fileInputRef.current?.click()}>
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" style={{width:20,height:20}}>
                  <path d="M21 15v4a2 2 0 01-2 2H5a2 2 0 01-2-2v-4"/>
                  <polyline points="17 8 12 3 7 8"/><line x1="12" y1="3" x2="12" y2="15"/>
                </svg>
                导入本地文件 (EPUB / PDF)
              </button>
              <input ref={fileInputRef} type="file" accept=".epub,.pdf" style={{display:'none'}} onChange={handleFileUpload} />

              <button className="btn btn-secondary" style={{justifyContent:'flex-start',padding:'12px 16px',borderRadius:10}} onClick={() => setShowUrlForm(true)}>
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" style={{width:20,height:20}}>
                  <circle cx="12" cy="12" r="10"/>
                  <line x1="2" y1="12" x2="22" y2="12"/>
                  <path d="M12 2a15.3 15.3 0 014 10 15.3 15.3 0 01-4 10 15.3 15.3 0 01-4-10 15.3 15.3 0 014-10z"/>
                </svg>
                通过链接添加 (EPUB / PDF / 网页)
              </button>

              <div style={{marginTop:4}}>
                <div style={{fontSize:13,color:'var(--text-secondary)',padding:'8px 0 6px',fontWeight:600}}>示例书籍</div>
                {SAMPLE_BOOKS.map(s => (
                  <button key={s.url} className="btn btn-ghost" style={{width:'100%',justifyContent:'flex-start',marginBottom:6,borderRadius:10,padding:'10px 14px'}} onClick={() => addSampleBook(s)}>
                    <span style={{marginRight:8}}>{s.format === 'epub' ? '📖' : s.format === 'pdf' ? '📄' : '🌐'}</span>
                    <span style={{fontSize:13}}>{s.title}</span>
                  </button>
                ))}
              </div>
            </div>
          </div>
        </div>
      )}

      {/* URL form */}
      {showUrlForm && (
        <div className="modal-overlay" onClick={() => { setShowUrlForm(false); setShowAdd(false); }}>
          <div className="modal-sheet" onClick={e => e.stopPropagation()}>
            <div className="modal-handle" />
            <div className="modal-title">通过链接添加</div>

            <div className="form-group">
              <label className="form-label">链接地址</label>
              <input
                className="form-input"
                type="url"
                placeholder="https://..."
                value={urlInput}
                onChange={e => setUrlInput(e.target.value)}
                autoFocus
              />
            </div>

            <div className="form-group">
              <label className="form-label">书名（可选）</label>
              <input
                className="form-input"
                type="text"
                placeholder="留空则使用链接"
                value={urlTitle}
                onChange={e => setUrlTitle(e.target.value)}
              />
            </div>

            <div className="form-group">
              <label className="form-label">格式</label>
              <select
                className="form-input"
                value={urlFormat}
                onChange={e => setUrlFormat(e.target.value)}
              >
                <option value="auto">自动识别</option>
                <option value="epub">EPUB</option>
                <option value="pdf">PDF</option>
                <option value="web">网页</option>
              </select>
            </div>

            <div className="form-actions">
              <button className="btn btn-secondary" onClick={() => setShowUrlForm(false)}>取消</button>
              <button className="btn btn-primary" onClick={handleAddUrl}>添加</button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
