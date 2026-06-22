import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import './index.css'
import './i18n/config'
import App from './App.tsx'
import SearchApp from './SearchApp.tsx'

const isSearch = window.location.pathname === '/search' || window.location.hash.includes('/search') || window.location.search.includes('search=1');

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    {isSearch ? <SearchApp /> : <App />}
  </StrictMode>,
)
