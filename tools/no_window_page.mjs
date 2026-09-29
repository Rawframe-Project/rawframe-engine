// What a web client's window side answers where no page gives one (D250):
// there is no page, as a browser's worker or Node has none. The client
// then runs the page's way (`start`), and only `play` needs a page, which
// Maul Window's maul-window.mjs gives it.
export const noWindowPage = new Proxy({ mwinWebHasDocument: () => 0 }, {
    get: (target, name) => target[name] ?? (() => {
        throw new Error(`${String(name)} needs the window's page side`);
    }),
});
