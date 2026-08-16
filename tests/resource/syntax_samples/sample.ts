interface User {
    name: string;
}

export function greet(user: User): string {
    const prefix = "Hello";
    return `${prefix}, ${user.name}`;
}
