import React, { useState, useEffect, createContext, useContext } from 'react';
import { BrowserRouter, Routes, Route, useNavigate, useLocation } from 'react-router-dom';
import Library from './pages/Library.jsx';
import CEFRTest from './pages/CEFRTest.jsx';
import Settings from './pages/Settings.jsx';
import Reader from './pages/Reader.jsx';
import { getSetting, setSetting } from './utils/db.js';

export const AppContext = createContext({});

export function useApp() {
  return useContext(AppContext);
}

function NavBar() {
  const navigate = useNavigate();
  const location = useLocation();
  const path = location.pathname;

  const tabs = [
    {
      path: '/',
      label: '书架',
      icon: (
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
          <path d="M4 19.5A2.5 2.5 0 016.5 17H20"/>
          <path d="M6.5 2H20v20H6.5A2.5 2.5 0 014 19.5v-15A2.5 2.5 0 016.5 2z"/>
        </svg>
      )
    },
    {
      path: '/test',
      label: '测试',
      icon: (
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
          <path d="M9 11l3 3L22 4"/>
          <path d="M21 12v7a2 2 0 01-2 2H5a2 2 0 01-2-2V5a2 2 0 012-2h11"/>
        </svg>
      )
    },
    {
      path: '/settings',
      label: '设置',
      icon: (
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
          <circle cx="12" cy="12" r="3"/>
          <path d="M19.4 15a1.65 1.65 0 00.33 1.82l.06.06a2 2 0 010 2.83 2 2 0 01-2.83 0l-.06-.06a1.65 1.65 0 00-1.82-.33 1.65 1.65 0 00-1 1.51V21a2 2 0 01-4 0v-.09A1.65 1.65 0 009 19.4a1.65 1.65 0 00-1.82.33l-.06.06a2 2 0 01-2.83-2.83l.06-.06A1.65 1.65 0 004.68 15a1.65 1.65 0 00-1.51-1H3a2 2 0 010-4h.09A1.65 1.65 0 004.6 9a1.65 1.65 0 00-.33-1.82l-.06-.06a2 2 0 012.83-2.83l.06.06A1.65 1.65 0 009 4.68a1.65 1.65 0 001-1.51V3a2 2 0 014 0v.09a1.65 1.65 0 001 1.51 1.65 1.65 0 001.82-.33l.06-.06a2 2 0 012.83 2.83l-.06.06A1.65 1.65 0 0019.4 9a1.65 1.65 0 001.51 1H21a2 2 0 010 4h-.09a1.65 1.65 0 00-1.51 1z"/>
        </svg>
      )
    }
  ];

  if (path.startsWith('/reader')) return null;

  return (
    <nav className="bottom-nav">
      {tabs.map(tab => (
        <button
          key={tab.path}
          className={path === tab.path ? 'active' : ''}
          onClick={() => navigate(tab.path)}
        >
          {tab.icon}
          {tab.label}
        </button>
      ))}
    </nav>
  );
}

export default function App() {
  const [theme, setTheme] = useState(() => getSetting('theme', 'light'));
  const [cefrLevel, setCefrLevel] = useState(() => getSetting('cefrLevel', 'B1'));
  const [apiConfig, setApiConfig] = useState(() => getSetting('apiConfig', {
    baseUrl: 'https://api.openai.com',
    apiKey: '',
    model: 'gpt-4o-mini'
  }));
  const [fontSize, setFontSize] = useState(() => getSetting('fontSize', 18));

  useEffect(() => {
    document.documentElement.setAttribute('data-theme', theme === 'light' ? '' : theme);
  }, [theme]);

  function updateTheme(t) {
    setTheme(t);
    setSetting('theme', t);
  }

  function updateCefrLevel(l) {
    setCefrLevel(l);
    setSetting('cefrLevel', l);
  }

  function updateApiConfig(c) {
    setApiConfig(c);
    setSetting('apiConfig', c);
  }

  function updateFontSize(s) {
    setFontSize(s);
    setSetting('fontSize', s);
  }

  const ctx = {
    theme, setTheme: updateTheme,
    cefrLevel, setCefrLevel: updateCefrLevel,
    apiConfig, setApiConfig: updateApiConfig,
    fontSize, setFontSize: updateFontSize
  };

  return (
    <AppContext.Provider value={ctx}>
      <BrowserRouter>
        <div className="app">
          <Routes>
            <Route path="/" element={<Library />} />
            <Route path="/test" element={<CEFRTest />} />
            <Route path="/settings" element={<Settings />} />
            <Route path="/reader/:bookId" element={<Reader />} />
          </Routes>
          <NavBar />
        </div>
      </BrowserRouter>
    </AppContext.Provider>
  );
}
