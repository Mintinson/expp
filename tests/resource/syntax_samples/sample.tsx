interface GreetingProps {
    name: string;
}

export const Greeting = ({name}: GreetingProps) => {
    const prefix = "Hello";
    return <main>{prefix}, {name}</main>;
};
