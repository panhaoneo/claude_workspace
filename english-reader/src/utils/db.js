import { openDB } from 'idb';

const DB_NAME = 'english-reader';
const DB_VERSION = 1;

let dbPromise;

function getDB() {
  if (!dbPromise) {
    dbPromise = openDB(DB_NAME, DB_VERSION, {
      upgrade(db) {
        if (!db.objectStoreNames.contains('books')) {
          const books = db.createObjectStore('books', { keyPath: 'id', autoIncrement: true });
          books.createIndex('format', 'format');
        }
        if (!db.objectStoreNames.contains('translations')) {
          db.createObjectStore('translations', { keyPath: 'word' });
        }
        if (!db.objectStoreNames.contains('progress')) {
          db.createObjectStore('progress', { keyPath: 'bookId' });
        }
        if (!db.objectStoreNames.contains('settings')) {
          db.createObjectStore('settings', { keyPath: 'key' });
        }
      }
    });
  }
  return dbPromise;
}

// Books
export async function saveBook(book) {
  const db = await getDB();
  return db.put('books', { ...book, addedAt: Date.now() });
}

export async function getBooks() {
  const db = await getDB();
  return db.getAll('books');
}

export async function getBook(id) {
  const db = await getDB();
  return db.get('books', id);
}

export async function deleteBook(id) {
  const db = await getDB();
  return db.delete('books', id);
}

// Translation cache
export async function getTranslation(word) {
  const db = await getDB();
  const entry = await db.get('translations', word.toLowerCase());
  if (!entry) return null;
  // Expire after 90 days
  if (Date.now() - entry.cachedAt > 90 * 86400000) {
    await db.delete('translations', word.toLowerCase());
    return null;
  }
  return entry;
}

export async function cacheTranslation(word, data) {
  const db = await getDB();
  return db.put('translations', { ...data, word: word.toLowerCase(), cachedAt: Date.now() });
}

export async function bulkCacheTranslations(entries) {
  const db = await getDB();
  const tx = db.transaction('translations', 'readwrite');
  const now = Date.now();
  for (const entry of entries) {
    tx.store.put({ ...entry, word: entry.word.toLowerCase(), cachedAt: now });
  }
  await tx.done;
}

export async function getCachedWords(words) {
  const db = await getDB();
  const tx = db.transaction('translations', 'readonly');
  const results = {};
  const now = Date.now();
  for (const word of words) {
    const entry = await tx.store.get(word.toLowerCase());
    if (entry && now - entry.cachedAt < 90 * 86400000) {
      results[word.toLowerCase()] = entry;
    }
  }
  return results;
}

// Reading progress
export async function saveProgress(bookId, data) {
  const db = await getDB();
  return db.put('progress', { bookId, ...data, updatedAt: Date.now() });
}

export async function getProgress(bookId) {
  const db = await getDB();
  return db.get('progress', bookId);
}

// Settings (fallback to localStorage)
export function getSetting(key, defaultValue = null) {
  try {
    const val = localStorage.getItem(`reader_${key}`);
    return val !== null ? JSON.parse(val) : defaultValue;
  } catch {
    return defaultValue;
  }
}

export function setSetting(key, value) {
  try {
    localStorage.setItem(`reader_${key}`, JSON.stringify(value));
  } catch {}
}
