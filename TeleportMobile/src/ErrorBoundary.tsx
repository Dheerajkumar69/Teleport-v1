/**
 * Error Boundary - React component for catching and handling errors
 * Provides graceful degradation and error reporting
 */
import React, { Component, ErrorInfo, ReactNode } from 'react';
import {
    View,
    Text,
    TouchableOpacity,
    StyleSheet,
    ScrollView,
} from 'react-native';

interface Props {
    children: ReactNode;
    fallback?: ReactNode;
    onError?: (error: Error, errorInfo: ErrorInfo) => void;
}

interface State {
    hasError: boolean;
    error: Error | null;
    errorInfo: ErrorInfo | null;
}

/**
 * Simple crash reporter - logs errors with context
 * Replace with Sentry/Crashlytics in production
 */
export function reportCrash(
    error: Error,
    context: Record<string, any> = {}
): void {
    const report = {
        timestamp: new Date().toISOString(),
        error: {
            name: error.name,
            message: error.message,
            stack: error.stack,
        },
        context,
        // Add device info, app version, etc. in production
    };

    console.error('[CrashReport]', JSON.stringify(report, null, 2));

    // TODO: In production, send to crash reporting service
    // Sentry.captureException(error, { extra: context });
}

/**
 * Error Boundary Component
 * Catches JavaScript errors anywhere in the child component tree
 */
class ErrorBoundary extends Component<Props, State> {
    constructor(props: Props) {
        super(props);
        this.state = {
            hasError: false,
            error: null,
            errorInfo: null,
        };
    }

    static getDerivedStateFromError(error: Error): Partial<State> {
        return { hasError: true, error };
    }

    componentDidCatch(error: Error, errorInfo: ErrorInfo): void {
        this.setState({ errorInfo });

        // Report to crash reporting
        reportCrash(error, {
            componentStack: errorInfo.componentStack,
        });

        // Call custom error handler
        this.props.onError?.(error, errorInfo);
    }

    handleRetry = (): void => {
        this.setState({
            hasError: false,
            error: null,
            errorInfo: null,
        });
    };

    render(): ReactNode {
        if (this.state.hasError) {
            if (this.props.fallback) {
                return this.props.fallback;
            }

            return (
                <View style={styles.container}>
                    <View style={styles.content}>
                        <Text style={styles.emoji}>💥</Text>
                        <Text style={styles.title}>Something went wrong</Text>
                        <Text style={styles.message}>
                            {this.state.error?.message || 'An unexpected error occurred'}
                        </Text>

                        <TouchableOpacity style={styles.button} onPress={this.handleRetry}>
                            <Text style={styles.buttonText}>Try Again</Text>
                        </TouchableOpacity>

                        {__DEV__ && this.state.errorInfo && (
                            <ScrollView style={styles.debugContainer}>
                                <Text style={styles.debugTitle}>Debug Info:</Text>
                                <Text style={styles.debugText}>
                                    {this.state.error?.stack}
                                </Text>
                            </ScrollView>
                        )}
                    </View>
                </View>
            );
        }

        return this.props.children;
    }
}

const styles = StyleSheet.create({
    container: {
        flex: 1,
        backgroundColor: '#000',
        justifyContent: 'center',
        alignItems: 'center',
        padding: 24,
    },
    content: {
        alignItems: 'center',
        maxWidth: 300,
    },
    emoji: {
        fontSize: 48,
        marginBottom: 16,
    },
    title: {
        color: '#fff',
        fontSize: 20,
        fontWeight: '600',
        marginBottom: 8,
        textAlign: 'center',
    },
    message: {
        color: '#888',
        fontSize: 14,
        textAlign: 'center',
        marginBottom: 24,
    },
    button: {
        backgroundColor: '#4ade80',
        paddingHorizontal: 32,
        paddingVertical: 14,
        borderRadius: 4,
    },
    buttonText: {
        color: '#000',
        fontSize: 16,
        fontWeight: '600',
    },
    debugContainer: {
        marginTop: 24,
        maxHeight: 200,
        width: '100%',
        backgroundColor: '#111',
        borderRadius: 4,
        padding: 12,
    },
    debugTitle: {
        color: '#ef4444',
        fontSize: 12,
        fontWeight: '600',
        marginBottom: 8,
    },
    debugText: {
        color: '#666',
        fontSize: 10,
        fontFamily: 'monospace',
    },
});

export default ErrorBoundary;
