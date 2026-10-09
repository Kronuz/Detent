// The curated docs navigation. A group whose first item is an `overview(...)` link
// renders its header as a link to that page (see components/SidebarSublist.astro);
// top-level groups start expanded, nested ones collapsed.

const overview = (link) => ({ label: 'Overview', link, attrs: { 'data-overview': '' } });
const group = (label, items, { collapsed = true } = {}) => ({ label, collapsed, items });
const pages = (...slugs) => slugs.map((slug) => ({ slug }));

export const sidebar = [
	group(
		'Getting Started',
		[
			...pages('welcome'),
			group('Quick-start', [
				overview('/quickstart'),
				...pages('basics', 'settings-and-flags', 'rollouts', 'group-overrides', 'next'),
			], { collapsed: false }),
			...pages('installation', 'benchmarks'),
		],
		{ collapsed: false },
	),
	group(
		'Guides',
		[
			overview('/guides'),
			...pages(
				'guides/cpp-application',
				'guides/live-updates',
				'guides/shell-and-agent',
				'guides/kill-switches',
				'guides/testing',
				'guides/python-and-rust',
				'guides/clustering',
				'guides/durable-authority',
				'guides/managed-delivery',
			),
		],
		{ collapsed: false },
	),
	group(
		'Reference Guide',
		[
			...pages('reference/knobs', 'reference/snapshot-format', 'reference/evaluation', 'reference/errors'),
			group('C++ API', [
				overview('/reference/cpp-api'),
				...pages('reference/cpp-api/client', 'reference/cpp-api/sources', 'reference/cpp-api/global-and-testing'),
			]),
			group('Bindings', [overview('/reference/bindings'), ...pages('reference/bindings/python', 'reference/bindings/rust')]),
			group('Command Line', [
				overview('/reference/cli'),
				...pages('reference/cli/detent', 'reference/cli/detent-agent', 'reference/cli/detentd', 'reference/cli/detent-authority-admin'),
			]),
			group('HTTP API', [
				overview('/reference/http-api'),
				...pages('reference/http-api/snapshot', 'reference/http-api/health', 'reference/http-api/authority'),
			]),
			...pages('reference/configuration', 'reference/observability', 'reference/limits'),
		],
		{ collapsed: false },
	),
	group(
		'Set up',
		[
			...pages(
				'operations/services',
				'operations/logging',
				'operations/clustering',
				'operations/authority',
				'operations/security',
				'operations/upgrading',
			),
		],
		{ collapsed: false },
	),
	group(
		'Architecture',
		[
			overview('/architecture'),
			...pages(
				'architecture/read-path',
				'architecture/update-path',
				'architecture/distributor',
				'architecture/agent',
				'architecture/raft',
				'architecture/authority',
				'architecture/delivery',
				'architecture/dependencies',
				'architecture/design-documents',
			),
		],
		{ collapsed: false },
	),
	group(
		'Miscellaneous',
		[...pages('roadmap', 'faq', 'contributing', 'help', 'about', 'copyright')],
		{ collapsed: false },
	),
];
