import { useSyncExternalStore } from 'react';
import { editor } from '../editor/editor.js';

const sub = fn => editor.subscribe(fn);
const get = () => editor.state;
export function useEditor() { return useSyncExternalStore(sub, get); }
export { editor };
