import './specimen-nav.css';

// Keep native links available without JavaScript; the compact picker is for
// narrow screens. Only the three local study pages can be selected.
const pages = new Set(['/index.html', '/frog.html', '/crab.html']);
for (const picker of document.querySelectorAll('.specimen-picker-select')) {
  picker.addEventListener('change', () => {
    if (pages.has(picker.value)) window.location.assign(picker.value);
  });
}
