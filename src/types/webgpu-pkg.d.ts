/** The `webgpu` npm package (dawn bindings) exposes globals as a property bag. */
declare module 'webgpu' {
  export const globals: Record<string, unknown>;
  export const create: (flags?: string[]) => Record<string, unknown>;
}
